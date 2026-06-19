/* mcp_test_tools.inc — Test MCP tools (included from mcp.c). */

#include "graph_buffer/graph_buffer.h"
#include "test_ingest/run_tests.h"
#include "test_ingest/format_dispatch.h"
#include "test_ingest/test_graph.h"
#include "test_ingest/test_result.h"
#include "test_ingest/junit_xml.h"
#include "test_ingest/report_scan.h"

static char *mcp_test_json_error(const char *code, const char *message) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return heap_strdup("{\"code\":\"internal\",\"message\":\"oom\"}");
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "code", code ? code : "error");
    yyjson_mut_obj_add_str(doc, root, "message", message ? message : "");
    char *json = yy_doc_to_str(doc);
    yyjson_mut_doc_free(doc);
    return json;
}

static char *mcp_test_json_error_diag(const char *code, const char *message, const char *cwd,
                                      int found_roots, int xml_count) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return heap_strdup("{\"code\":\"internal\",\"message\":\"oom\"}");
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "code", code ? code : "error");
    yyjson_mut_obj_add_str(doc, root, "message", message ? message : "");
    if (cwd && cwd[0]) {
        yyjson_mut_obj_add_str(doc, root, "cwd", cwd);
    }
    yyjson_mut_obj_add_int(doc, root, "report_roots_found", found_roots);
    yyjson_mut_obj_add_int(doc, root, "xml_files_found", xml_count);
    yyjson_mut_obj_add_str(
        doc, root, "hint",
        found_roots == 0
            ? "No build/test-results or target/surefire-reports under cwd (searched submodules). "
              "Run tests first, set report_dir, or point cwd at the Gradle module."
            : "Report directories exist but no parseable JUnit XML was found. "
              "Check compile failures or set report_dir explicitly.");
    char *json = yy_doc_to_str(doc);
    yyjson_mut_doc_free(doc);
    return json;
}

static char *mcp_shell_quote_single(const char *s) {
    if (!s) {
        s = "";
    }
    size_t need = 3;
    for (const char *p = s; *p; p++) {
        need += (*p == '\'') ? 4 : 1;
    }
    char *out = malloc(need);
    if (!out) {
        return NULL;
    }
    char *w = out;
    *w++ = '\'';
    for (const char *p = s; *p; p++) {
        if (*p == '\'') {
            memcpy(w, "'\\''", 4);
            w += 4;
        } else {
            *w++ = *p;
        }
    }
    *w++ = '\'';
    *w = '\0';
    return out;
}

static char *mcp_build_test_command(const char *base, const char *args_json) {
    if (!base || !base[0]) {
        return NULL;
    }
    yyjson_doc *doc = yyjson_read(args_json, strlen(args_json), 0);
    if (!doc) {
        return heap_strdup(base);
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *tests = yyjson_obj_get(root, "tests");
    if (!yyjson_is_arr(tests) || yyjson_arr_size(tests) == 0) {
        yyjson_doc_free(doc);
        return heap_strdup(base);
    }
    size_t cap = strlen(base) + 64;
    size_t idx, max;
    yyjson_val *val;
    yyjson_arr_foreach(tests, idx, max, val) {
        if (yyjson_is_str(val)) {
            cap += strlen(yyjson_get_str(val)) + 16;
        }
    }
    char *cmd = malloc(cap);
    if (!cmd) {
        yyjson_doc_free(doc);
        return NULL;
    }
    snprintf(cmd, cap, "%s", base);
    size_t used = strlen(cmd);
    yyjson_arr_foreach(tests, idx, max, val) {
        if (!yyjson_is_str(val)) {
            continue;
        }
        char *quoted = mcp_shell_quote_single(yyjson_get_str(val));
        if (!quoted) {
            free(cmd);
            yyjson_doc_free(doc);
            return NULL;
        }
        int nw = snprintf(cmd + used, cap - used, " --tests %s", quoted);
        free(quoted);
        if (nw < 0 || (size_t)nw >= cap - used) {
            free(cmd);
            yyjson_doc_free(doc);
            return NULL;
        }
        used += (size_t)nw;
    }
    yyjson_doc_free(doc);
    return cmd;
}

typedef struct {
    cbm_test_session_t *best;
    const char *filter_project;
} mcp_latest_session_ctx_t;

static void mcp_latest_session_cb(const char *key, void *value, void *userdata) {
    (void)key;
    mcp_latest_session_ctx_t *ctx = userdata;
    cbm_test_session_t *sess = (cbm_test_session_t *)value;
    if (!sess) {
        return;
    }
    if (ctx->filter_project && ctx->filter_project[0]) {
        const char *sp = sess->project ? sess->project : "";
        if (strcmp(sp, ctx->filter_project) != 0) {
            return;
        }
    }
    if (!ctx->best || sess->created_at > ctx->best->created_at) {
        ctx->best = sess;
    }
}

static cbm_test_session_t *mcp_find_latest_session(cbm_mcp_server_t *srv, const char *project) {
    if (!srv || !srv->test_sessions) {
        return NULL;
    }
    mcp_latest_session_ctx_t ctx = {.best = NULL, .filter_project = project};
    cbm_ht_foreach(srv->test_sessions, mcp_latest_session_cb, &ctx);
    return ctx.best;
}

static const char *mcp_map_run_exec_error(const char *err) {
    if (!err) {
        return "test_run_failed";
    }
    if (strstr(err, "whitelisted")) {
        return "invalid_command";
    }
    if (strstr(err, "invalid shell token") || strstr(err, "invalid cwd") || strstr(err, "cwd is empty") ||
        strstr(err, "command is empty")) {
        return "invalid_argument";
    }
    return "test_run_failed";
}

static void mcp_append_failures(yyjson_mut_doc *doc, yyjson_mut_val *arr, const cbm_test_result_t *res) {
    if (!res) {
        return;
    }
    for (size_t si = 0; si < res->suite_count; si++) {
        const cbm_test_suite_t *su = &res->suites[si];
        for (size_t ci = 0; ci < su->case_count; ci++) {
            const cbm_test_case_t *tc = &su->cases[ci];
            if (tc->status != CBM_TEST_STATUS_FAILED && tc->status != CBM_TEST_STATUS_ERROR) {
                continue;
            }
            yyjson_mut_val *o = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_str(doc, o, "suite", su->name ? su->name : "");
            yyjson_mut_obj_add_str(doc, o, "name", tc->name ? tc->name : "");
            if (tc->message && tc->message[0]) {
                yyjson_mut_obj_add_str(doc, o, "message", tc->message);
            }
            if (tc->type && tc->type[0]) {
                yyjson_mut_obj_add_str(doc, o, "type", tc->type);
            }
            if (tc->location && tc->location[0]) {
                yyjson_mut_obj_add_str(doc, o, "location", tc->location);
            }
            yyjson_mut_arr_add_val(arr, o);
        }
    }
}

static char *mcp_build_run_json(const cbm_test_result_t *res, const char *run_id, bool persisted,
                                const char *format_str, bool output_truncated) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "run_id", run_id ? run_id : "");
    yyjson_mut_obj_add_bool(doc, root, "persisted", persisted);
    yyjson_mut_obj_add_str(doc, root, "format", format_str ? format_str : "auto");
    if (res) {
        yyjson_mut_obj_add_int(doc, root, "total", res->total);
        yyjson_mut_obj_add_int(doc, root, "passed", res->passed);
        yyjson_mut_obj_add_int(doc, root, "failed", res->failed);
        yyjson_mut_obj_add_int(doc, root, "skipped", res->skipped);
        yyjson_mut_obj_add_int(doc, root, "errors", res->errors);
        yyjson_mut_obj_add_real(doc, root, "duration_ms", res->duration_ms);
    } else {
        yyjson_mut_obj_add_int(doc, root, "total", 0);
        yyjson_mut_obj_add_int(doc, root, "passed", 0);
        yyjson_mut_obj_add_int(doc, root, "failed", 0);
        yyjson_mut_obj_add_int(doc, root, "skipped", 0);
        yyjson_mut_obj_add_int(doc, root, "errors", 0);
        yyjson_mut_obj_add_real(doc, root, "duration_ms", 0.0);
    }
    yyjson_mut_obj_add_bool(doc, root, "output_truncated", output_truncated);
    yyjson_mut_val *fail_arr = yyjson_mut_arr(doc);
    mcp_append_failures(doc, fail_arr, res);
    yyjson_mut_obj_add_val(doc, root, "failures", fail_arr);
    char *out = yy_doc_to_str(doc);
    yyjson_mut_doc_free(doc);
    return out;
}

typedef struct {
    char *suite;
    char *name;
    char *status;
    double duration_ms;
    char *message;
    char *location;
} mcp_tc_row_t;

static void mcp_tc_row_free(mcp_tc_row_t *r) {
    free(r->suite);
    free(r->name);
    free(r->status);
    free(r->message);
    free(r->location);
    memset(r, 0, sizeof(*r));
}

static int mcp_tc_row_cmp(const void *a, const void *b) {
    const mcp_tc_row_t *x = a;
    const mcp_tc_row_t *y = b;
    int c = strcmp(x->suite ? x->suite : "", y->suite ? y->suite : "");
    if (c != 0) {
        return c;
    }
    return strcmp(x->name ? x->name : "", y->name ? y->name : "");
}

static char *mcp_suite_for_case(const cbm_gbuf_t *gb, int64_t case_id) {
    const cbm_gbuf_edge_t **edges = NULL;
    int ec = 0;
    if (cbm_gbuf_find_edges_by_target_type(gb, case_id, "CONTAINS", &edges, &ec) != 0) {
        return heap_strdup("");
    }
    for (int i = 0; i < ec; i++) {
        const cbm_gbuf_node_t *src = cbm_gbuf_find_by_id(gb, edges[i]->source_id);
        if (src && src->label && strcmp(src->label, "TestSuite") == 0) {
            return heap_strdup(src->name ? src->name : "");
        }
    }
    return heap_strdup("");
}

static char *handle_run_tests(cbm_mcp_server_t *srv, const char *args) {
    test_sessions_evict(srv);
    char *command = cbm_mcp_get_string_arg(args, "command");
    char *cwd = cbm_mcp_get_string_arg(args, "cwd");
    char *project = cbm_mcp_get_string_arg(args, "project");
    char *run_id_in = cbm_mcp_get_string_arg(args, "run_id");
    char *format_in = cbm_mcp_get_string_arg(args, "format");
    bool persist = cbm_mcp_get_bool_arg(args, "persist");

    if (!command || !*command || !cwd || !*cwd) {
        free(command);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        char *ej = mcp_test_json_error("invalid_argument", "command and cwd are required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }

    bool want_persist = (run_id_in && run_id_in[0]) || persist;
    char *command_exec = mcp_build_test_command(command, args);
    if (!command_exec) {
        free(command);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        char *ej = mcp_test_json_error("internal", "failed to build test command");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    free(command);
    command = command_exec;

    char *stdout_buf = NULL;
    size_t out_len = 0;
    int exit_code = 0;
    char *exec_err = NULL;
    int xrc = cbm_run_tests_exec(command, cwd, &stdout_buf, &out_len, &exit_code, &exec_err);
    if (xrc != 0) {
        const char *code = mcp_map_run_exec_error(exec_err);
        char *ej = mcp_test_json_error(code, exec_err ? exec_err : "run failed");
        free(command);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        free(stdout_buf);
        free(exec_err);
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    free(exec_err);

    const char *fmt_use = (format_in && format_in[0]) ? format_in : "auto";
    cbm_test_result_t *res = cbm_dispatch_format(fmt_use, cwd, stdout_buf, out_len);
    free(stdout_buf);
    if (!res) {
        free(command);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        char *ej = mcp_test_json_error("test_run_failed", "failed to parse test output");
        char *rr = cbm_mcp_text_result(ej, true);
        free(ej);
        return rr;
    }
    if (!res->command || !res->command[0]) {
        free(res->command);
        res->command = heap_strdup(command);
    }

    char run_key[CBM_SZ_64];
    const char *rid_use = NULL;
    if (want_persist) {
        if (run_id_in && run_id_in[0]) {
            rid_use = run_id_in;
        } else {
            snprintf(run_key, sizeof(run_key), "run_%ld_%04x", (long)time(NULL), rand() & 0xFFFF);
            rid_use = run_key;
        }
    } else {
        snprintf(run_key, sizeof(run_key), "ephemeral_%ld", (long)time(NULL));
        rid_use = run_key;
    }

    const char *proj_gbuf = (project && project[0]) ? project : "_";
    cbm_gbuf_t *gb = cbm_gbuf_new(proj_gbuf, cwd);
    if (!gb) {
        cbm_test_result_free(res);
        free(command);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        char *ej = mcp_test_json_error("internal", "failed to allocate graph buffer");
        char *rr = cbm_mcp_text_result(ej, true);
        free(ej);
        return rr;
    }
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj_store = (project && project[0]) ? project : NULL;
    int64_t run_nid = 0;
    if (cbm_test_graph_build(gb, st, proj_store, rid_use, res, &run_nid) != 0) {
        cbm_gbuf_free(gb);
        cbm_test_result_free(res);
        free(command);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        char *ej = mcp_test_json_error("internal", "failed to build test graph");
        char *rr = cbm_mcp_text_result(ej, true);
        free(ej);
        return rr;
    }

    const char *fmt_out = (res->format && res->format[0]) ? res->format : fmt_use;
    if (!want_persist) {
        char *json = mcp_build_run_json(res, rid_use, false, fmt_out, false);
        cbm_gbuf_free(gb);
        cbm_test_result_free(res);
        free(command);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        if (!json) {
            return cbm_mcp_text_result("{}", false);
        }
        char *rr = cbm_mcp_text_result(json, false);
        free(json);
        return rr;
    }

    time_t now = time(NULL);
    cbm_test_session_t *sess =
        cbm_test_session_new(run_id_in && run_id_in[0] ? run_id_in : NULL, project, gb, run_nid,
                             res->total, res->passed, res->failed, res->skipped, res->errors,
                             res->duration_ms, command, fmt_out, now);
    free(command);
    free(cwd);
    free(project);
    free(run_id_in);
    free(format_in);
    if (!sess) {
        cbm_test_result_free(res);
        char *ej = mcp_test_json_error("internal", "failed to allocate session");
        char *rr = cbm_mcp_text_result(ej, true);
        free(ej);
        return rr;
    }
    if (cbm_ht_set(srv->test_sessions, sess->run_id, sess) != NULL) {
        cbm_test_session_free(sess);
        cbm_test_result_free(res);
        char *ej = mcp_test_json_error("internal", "duplicate run_id");
        char *rr = cbm_mcp_text_result(ej, true);
        free(ej);
        return rr;
    }
    char *json = mcp_build_run_json(res, sess->run_id, true, fmt_out, false);
    cbm_test_result_free(res);
    if (!json) {
        cbm_ht_delete(srv->test_sessions, sess->run_id);
        cbm_test_session_free(sess);
        return cbm_mcp_text_result("{}", false);
    }
    char *rr = cbm_mcp_text_result(json, false);
    free(json);
    return rr;
}

static char *handle_ingest_test_reports(cbm_mcp_server_t *srv, const char *args) {
    test_sessions_evict(srv);
    char *cwd = cbm_mcp_get_string_arg(args, "cwd");
    char *project = cbm_mcp_get_string_arg(args, "project");
    char *run_id_in = cbm_mcp_get_string_arg(args, "run_id");
    char *format_in = cbm_mcp_get_string_arg(args, "format");
    char *report_dir = cbm_mcp_get_string_arg(args, "report_dir");
    bool persist = cbm_mcp_get_bool_arg(args, "persist");

    if (!cwd || !*cwd) {
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        free(report_dir);
        char *ej = mcp_test_json_error("invalid_argument", "cwd is required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    if (report_dir && report_dir[0] && !cbm_validate_shell_arg(report_dir)) {
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        free(report_dir);
        char *ej = mcp_test_json_error("invalid_argument", "invalid report_dir");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }

    cbm_test_result_t *res = NULL;
    if (report_dir && report_dir[0]) {
        res = cbm_parse_junit_xml_dir(report_dir);
    } else {
        const char *fmt_use = (format_in && format_in[0]) ? format_in : "auto";
        res = cbm_dispatch_format(fmt_use, cwd, NULL, 0);
    }
    free(report_dir);

    if (!res) {
        int found_roots = 0;
        int xml_count = 0;
        (void)cbm_probe_report_roots(cwd, &found_roots, &xml_count);
        char *cwd_copy = heap_strdup(cwd);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        char *ej = mcp_test_json_error_diag("test_run_failed", "failed to parse test reports",
                                            cwd_copy ? cwd_copy : "", found_roots, xml_count);
        free(cwd_copy);
        char *rr = cbm_mcp_text_result(ej, true);
        free(ej);
        return rr;
    }
    if (!res->command || !res->command[0]) {
        free(res->command);
        res->command = heap_strdup("ingest_test_reports");
    }

    bool want_persist = (run_id_in && run_id_in[0]) || persist;
    char run_key[CBM_SZ_64];
    const char *rid_use = NULL;
    if (want_persist) {
        if (run_id_in && run_id_in[0]) {
            rid_use = run_id_in;
        } else {
            snprintf(run_key, sizeof(run_key), "run_%ld_%04x", (long)time(NULL), rand() & 0xFFFF);
            rid_use = run_key;
        }
    } else {
        snprintf(run_key, sizeof(run_key), "ephemeral_%ld", (long)time(NULL));
        rid_use = run_key;
    }

    const char *proj_gbuf = (project && project[0]) ? project : "_";
    cbm_gbuf_t *gb = cbm_gbuf_new(proj_gbuf, cwd);
    if (!gb) {
        cbm_test_result_free(res);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        char *ej = mcp_test_json_error("internal", "failed to allocate graph buffer");
        char *rr = cbm_mcp_text_result(ej, true);
        free(ej);
        return rr;
    }
    cbm_store_t *st = cbm_mcp_server_store(srv);
    const char *proj_store = (project && project[0]) ? project : NULL;
    int64_t run_nid = 0;
    if (cbm_test_graph_build(gb, st, proj_store, rid_use, res, &run_nid) != 0) {
        cbm_gbuf_free(gb);
        cbm_test_result_free(res);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        char *ej = mcp_test_json_error("internal", "failed to build test graph");
        char *rr = cbm_mcp_text_result(ej, true);
        free(ej);
        return rr;
    }

    const char *fmt_out = (res->format && res->format[0]) ? res->format : "auto";
    if (!want_persist) {
        char *json = mcp_build_run_json(res, rid_use, false, fmt_out, false);
        cbm_gbuf_free(gb);
        cbm_test_result_free(res);
        free(cwd);
        free(project);
        free(run_id_in);
        free(format_in);
        char *rr = cbm_mcp_text_result(json ? json : "{}", false);
        free(json);
        return rr;
    }

    time_t now = time(NULL);
    cbm_test_session_t *sess =
        cbm_test_session_new(run_id_in && run_id_in[0] ? run_id_in : NULL, project, gb, run_nid,
                             res->total, res->passed, res->failed, res->skipped, res->errors,
                             res->duration_ms, "ingest_test_reports", fmt_out, now);
    free(cwd);
    free(project);
    free(run_id_in);
    free(format_in);
    if (!sess) {
        cbm_test_result_free(res);
        char *ej = mcp_test_json_error("internal", "failed to allocate session");
        char *rr = cbm_mcp_text_result(ej, true);
        free(ej);
        return rr;
    }
    if (cbm_ht_set(srv->test_sessions, sess->run_id, sess) != NULL) {
        cbm_test_session_free(sess);
        cbm_test_result_free(res);
        char *ej = mcp_test_json_error("internal", "duplicate run_id");
        char *rr = cbm_mcp_text_result(ej, true);
        free(ej);
        return rr;
    }
    char *json = mcp_build_run_json(res, sess->run_id, true, fmt_out, false);
    cbm_test_result_free(res);
    char *rr = cbm_mcp_text_result(json ? json : "{}", false);
    free(json);
    return rr;
}

static char *handle_query_test_results(cbm_mcp_server_t *srv, const char *args) {
    test_sessions_evict(srv);
    char *run_id = cbm_mcp_get_string_arg(args, "run_id");
    char *project_f = cbm_mcp_get_string_arg(args, "project");
    char *status_f = cbm_mcp_get_string_arg(args, "status");
    char *name_pat = cbm_mcp_get_string_arg(args, "name_pattern");
    char *suite_pat = cbm_mcp_get_string_arg(args, "suite_pattern");
    bool use_latest = cbm_mcp_get_bool_arg(args, "latest");
    int limit = cbm_mcp_get_int_arg(args, "limit", 50);
    int offset = cbm_mcp_get_int_arg(args, "offset", 0);
    if (limit < 0) {
        limit = 50;
    }
    if (offset < 0) {
        offset = 0;
    }

    cbm_test_session_t *sess = NULL;
    bool run_id_is_latest = run_id && strcmp(run_id, "latest") == 0;
    if (use_latest || run_id_is_latest || !run_id || !run_id[0]) {
        if (use_latest || run_id_is_latest) {
            sess = mcp_find_latest_session(srv, project_f);
            if (!sess) {
                free(run_id);
                free(project_f);
                free(status_f);
                free(name_pat);
                free(suite_pat);
                char *ej = mcp_test_json_error("run_id_not_found",
                                               "no persisted test runs for project");
                char *res = cbm_mcp_text_result(ej, true);
                free(ej);
                return res;
            }
        } else {
            free(run_id);
            free(project_f);
            free(status_f);
            free(name_pat);
            free(suite_pat);
            char *ej = mcp_test_json_error("invalid_argument", "run_id is required");
            char *res = cbm_mcp_text_result(ej, true);
            free(ej);
            return res;
        }
    } else {
        sess = cbm_ht_get(srv->test_sessions, run_id);
    }
    free(run_id);
    if (!sess) {
        free(project_f);
        free(status_f);
        free(name_pat);
        free(suite_pat);
        char *ej = mcp_test_json_error("run_id_not_found", "unknown run_id");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    free(project_f);
    sess->last_access = time(NULL);

    cbm_regex_t re_name;
    cbm_regex_t re_suite;
    bool have_name = false;
    bool have_suite = false;
    if (name_pat && name_pat[0]) {
        if (cbm_regcomp(&re_name, name_pat, CBM_REG_EXTENDED | CBM_REG_NOSUB) == CBM_REG_OK) {
            have_name = true;
        } else {
            free(status_f);
            free(name_pat);
            free(suite_pat);
            char *ej = mcp_test_json_error("invalid_argument", "invalid name_pattern regex");
            char *res = cbm_mcp_text_result(ej, true);
            free(ej);
            return res;
        }
    }
    if (suite_pat && suite_pat[0]) {
        if (cbm_regcomp(&re_suite, suite_pat, CBM_REG_EXTENDED | CBM_REG_NOSUB) == CBM_REG_OK) {
            have_suite = true;
        } else {
            if (have_name) {
                cbm_regfree(&re_name);
            }
            free(status_f);
            free(name_pat);
            free(suite_pat);
            char *ej = mcp_test_json_error("invalid_argument", "invalid suite_pattern regex");
            char *res = cbm_mcp_text_result(ej, true);
            free(ej);
            return res;
        }
    }

    const cbm_gbuf_t *gb = sess->graph;
    const cbm_gbuf_node_t **cases = NULL;
    int ncase = 0;
    if (cbm_gbuf_find_by_label(gb, "TestCase", &cases, &ncase) != 0) {
        cases = NULL;
        ncase = 0;
    }

    mcp_tc_row_t *rows = NULL;
    size_t nrows = 0;
    size_t cap = 0;
    for (int i = 0; i < ncase; i++) {
        const cbm_gbuf_node_t *n = cases[i];
        yyjson_doc *pdoc = yyjson_read(n->properties_json, strlen(n->properties_json), 0);
        if (!pdoc) {
            continue;
        }
        yyjson_val *root = yyjson_doc_get_root(pdoc);
        yyjson_val *st = yyjson_obj_get(root, "status");
        const char *ststr = st && yyjson_is_str(st) ? yyjson_get_str(st) : "passed";
        if (status_f && status_f[0] && strcmp(ststr, status_f) != 0) {
            yyjson_doc_free(pdoc);
            continue;
        }
        char *suite = mcp_suite_for_case(gb, n->id);
        if (have_suite && cbm_regexec(&re_suite, suite ? suite : "", 0, NULL, 0) != CBM_REG_OK) {
            free(suite);
            yyjson_doc_free(pdoc);
            continue;
        }
        const char *nm = n->name ? n->name : "";
        if (have_name && cbm_regexec(&re_name, nm, 0, NULL, 0) != CBM_REG_OK) {
            free(suite);
            yyjson_doc_free(pdoc);
            continue;
        }

        if (nrows == cap) {
            size_t ncap = cap ? cap * 2 : (size_t)CBM_SZ_32;
            void *nr = realloc(rows, ncap * sizeof(*rows));
            if (!nr) {
                free(suite);
                yyjson_doc_free(pdoc);
                goto oom_fail;
            }
            rows = nr;
            cap = ncap;
        }
        mcp_tc_row_t *row = &rows[nrows++];
        memset(row, 0, sizeof(*row));
        row->suite = suite;
        row->name = heap_strdup(nm);
        row->status = heap_strdup(ststr);
        yyjson_val *dm = yyjson_obj_get(root, "duration_ms");
        row->duration_ms = (dm && yyjson_is_real(dm)) ? yyjson_get_real(dm) : 0.0;
        yyjson_val *msg = yyjson_obj_get(root, "message");
        if (msg && yyjson_is_str(msg)) {
            row->message = heap_strdup(yyjson_get_str(msg));
        }
        yyjson_val *loc = yyjson_obj_get(root, "location");
        if (loc && yyjson_is_str(loc)) {
            row->location = heap_strdup(yyjson_get_str(loc));
        }
        yyjson_doc_free(pdoc);
    }

    if (have_name) {
        cbm_regfree(&re_name);
    }
    if (have_suite) {
        cbm_regfree(&re_suite);
    }
    free(status_f);
    free(name_pat);
    free(suite_pat);

    qsort(rows, nrows, sizeof(*rows), mcp_tc_row_cmp);

    size_t total_matching = nrows;
    size_t start = (size_t)offset;
    if (start > nrows) {
        start = nrows;
    }
    size_t max_take = (size_t)limit;
    if (max_take > nrows - start) {
        max_take = nrows - start;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        goto oom_fail;
    }
    yyjson_mut_val *rootv = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, rootv);
    yyjson_mut_obj_add_str(doc, rootv, "run_id", sess->run_id);
    yyjson_mut_obj_add_int(doc, rootv, "total_matching", (int64_t)total_matching);
    yyjson_mut_obj_add_int(doc, rootv, "returned", (int64_t)max_take);
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < max_take; i++) {
        const mcp_tc_row_t *row = &rows[start + i];
        yyjson_mut_val *o = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, o, "suite", row->suite ? row->suite : "");
        yyjson_mut_obj_add_str(doc, o, "name", row->name ? row->name : "");
        yyjson_mut_obj_add_str(doc, o, "status", row->status ? row->status : "");
        yyjson_mut_obj_add_real(doc, o, "duration_ms", row->duration_ms);
        if (row->message && row->message[0]) {
            yyjson_mut_obj_add_str(doc, o, "message", row->message);
        }
        if (row->location && row->location[0]) {
            yyjson_mut_obj_add_str(doc, o, "location", row->location);
        }
        yyjson_mut_arr_add_val(arr, o);
    }
    yyjson_mut_obj_add_val(doc, rootv, "results", arr);
    char *json = yy_doc_to_str(doc);
    yyjson_mut_doc_free(doc);
    for (size_t j = 0; j < nrows; j++) {
        mcp_tc_row_free(&rows[j]);
    }
    free(rows);
    char *res = cbm_mcp_text_result(json ? json : "{}", false);
    free(json);
    return res;

oom_fail:
    if (have_name) {
        cbm_regfree(&re_name);
    }
    if (have_suite) {
        cbm_regfree(&re_suite);
    }
    free(status_f);
    free(name_pat);
    free(suite_pat);
    if (rows) {
        for (size_t i = 0; i < nrows; i++) {
            mcp_tc_row_free(&rows[i]);
        }
        free(rows);
    }
    char *ej = mcp_test_json_error("internal", "out of memory");
    char *out_err = cbm_mcp_text_result(ej, true);
    free(ej);
    return out_err;
}


typedef struct {
    cbm_test_session_t **items;
    size_t n;
    size_t cap;
    const char *filter_project;
} mcp_list_sessions_ctx_t;

static void mcp_list_sessions_cb(const char *key, void *value, void *userdata) {
    (void)key;
    mcp_list_sessions_ctx_t *ctx = userdata;
    cbm_test_session_t *sess = (cbm_test_session_t *)value;
    if (ctx->filter_project && ctx->filter_project[0]) {
        const char *sp = sess->project ? sess->project : "";
        if (strcmp(sp, ctx->filter_project) != 0) {
            return;
        }
    }
    if (ctx->n >= ctx->cap) {
        return;
    }
    ctx->items[ctx->n++] = sess;
}

static int mcp_list_sessions_cmp_desc(const void *a, const void *b) {
    cbm_test_session_t *sx = *(cbm_test_session_t **)a;
    cbm_test_session_t *sy = *(cbm_test_session_t **)b;
    if (sx->created_at > sy->created_at) {
        return -1;
    }
    if (sx->created_at < sy->created_at) {
        return 1;
    }
    const char *rx = sx->run_id ? sx->run_id : "";
    const char *ry = sy->run_id ? sy->run_id : "";
    return strcmp(rx, ry);
}

static char *handle_list_test_runs(cbm_mcp_server_t *srv, const char *args) {
    test_sessions_evict(srv);
    char *proj_f = cbm_mcp_get_string_arg(args, "project");

    mcp_list_sessions_ctx_t ctx = {0};
    uint32_t hc = cbm_ht_count(srv->test_sessions);
    ctx.cap = (size_t)(hc ? hc : 4);
    if (ctx.cap > 0) {
        ctx.items = calloc(ctx.cap, sizeof(*ctx.items));
        if (!ctx.items) {
            free(proj_f);
            char *ej = mcp_test_json_error("internal", "out of memory");
            char *res = cbm_mcp_text_result(ej, true);
            free(ej);
            return res;
        }
    }
    if (proj_f && proj_f[0]) {
        ctx.filter_project = proj_f;
    }
    cbm_ht_foreach(srv->test_sessions, mcp_list_sessions_cb, &ctx);
    free(proj_f);

    if (ctx.items && ctx.n > 1) {
        qsort(ctx.items, ctx.n, sizeof(*ctx.items), mcp_list_sessions_cmp_desc);
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        free(ctx.items);
        char *ej = mcp_test_json_error("internal", "out of memory");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val *runs = yyjson_mut_arr(doc);
    for (size_t i = 0; i < ctx.n; i++) {
        cbm_test_session_t *s = ctx.items[i];
        yyjson_mut_val *o = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, o, "run_id", s->run_id ? s->run_id : "");
        yyjson_mut_obj_add_str(doc, o, "project", s->project ? s->project : "");
        yyjson_mut_obj_add_str(doc, o, "command", s->command[0] ? s->command : "");
        yyjson_mut_obj_add_str(doc, o, "format", s->format[0] ? s->format : "");
        yyjson_mut_obj_add_int(doc, o, "total", s->total);
        yyjson_mut_obj_add_int(doc, o, "passed", s->passed);
        yyjson_mut_obj_add_int(doc, o, "failed", s->failed);
        yyjson_mut_obj_add_int(doc, o, "skipped", s->skipped);
        yyjson_mut_obj_add_int(doc, o, "errors", s->errors);
        yyjson_mut_obj_add_real(doc, o, "duration_ms", s->duration_ms);
        yyjson_mut_obj_add_int(doc, o, "created_at", (int64_t)s->created_at);
        yyjson_mut_obj_add_int(doc, o, "last_access", (int64_t)s->last_access);
        yyjson_mut_arr_add_val(runs, o);
    }
    free(ctx.items);
    yyjson_mut_obj_add_val(doc, root, "runs", runs);
    char *json = yy_doc_to_str(doc);
    yyjson_mut_doc_free(doc);
    return cbm_mcp_text_result(json ? json : "{}", false);
}

typedef struct {
    int64_t case_id;
    char *suite;
    char *name;
    char *status;
    char *message;
    char *location;
} mcp_trace_case_t;

static void mcp_trace_case_free(mcp_trace_case_t *c) {
    free(c->suite);
    free(c->name);
    free(c->status);
    free(c->message);
    free(c->location);
    memset(c, 0, sizeof(*c));
}

static int mcp_trace_case_cmp(const void *a, const void *b) {
    const mcp_trace_case_t *x = a;
    const mcp_trace_case_t *y = b;
    int d = strcmp(x->suite ? x->suite : "", y->suite ? y->suite : "");
    if (d != 0) {
        return d;
    }
    return strcmp(x->name ? x->name : "", y->name ? y->name : "");
}

static bool mcp_include_errors_from_args(const char *args) {
    if (!args) {
        return true;
    }
    yyjson_doc *doc = yyjson_read(args, strlen(args), 0);
    if (!doc) {
        return true;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *v = yyjson_obj_get(root, "include_errors");
    bool out = true;
    if (v && yyjson_is_bool(v)) {
        out = yyjson_get_bool(v);
    }
    yyjson_doc_free(doc);
    return out;
}

static char *handle_trace_test_failures(cbm_mcp_server_t *srv, const char *args) {
    test_sessions_evict(srv);
    char *run_id = cbm_mcp_get_string_arg(args, "run_id");
    char *project = cbm_mcp_get_string_arg(args, "project");
    bool include_errors = mcp_include_errors_from_args(args);
    int depth = cbm_mcp_get_int_arg(args, "depth", MCP_DEFAULT_BFS_DEPTH);
    if (depth <= 0) {
        depth = MCP_DEFAULT_BFS_DEPTH;
    }
    if (depth > MCP_MAX_DEPTH) {
        depth = MCP_MAX_DEPTH;
    }

    cbm_traverse_result_t *tr_keep = NULL;
    size_t tr_keep_n = 0;
    size_t tr_keep_cap = 0;
    cbm_node_t *sfn_keep = NULL;
    size_t sfn_keep_n = 0;
    size_t sfn_keep_cap = 0;
    yyjson_mut_doc *doc = NULL;

    if (!run_id || !run_id[0]) {
        free(run_id);
        free(project);
        char *ej = mcp_test_json_error("invalid_argument", "run_id is required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    if (!project || !project[0]) {
        free(run_id);
        free(project);
        char *ej = mcp_test_json_error("invalid_argument", "project is required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }

    char *idx_err = NULL;
    cbm_store_t *store = cbm_mcp_server_store(srv);
    if (store) {
        idx_err = verify_project_indexed(store, project);
    }
    if (!store || idx_err) {
        if (idx_err) {
            free(idx_err);
        }
        store = resolve_store(srv, project);
        if (!store) {
            char *_err = build_project_list_error("project not found or not indexed");
            char *_res = cbm_mcp_text_result(_err, true);
            free(_err);
            free(run_id);
            free(project);
            return _res;
        }
        idx_err = verify_project_indexed(store, project);
        if (idx_err) {
            free(run_id);
            free(project);
            return idx_err;
        }
    }

    cbm_test_session_t *sess = cbm_ht_get(srv->test_sessions, run_id);
    free(run_id);
    if (!sess) {
        free(project);
        char *ej = mcp_test_json_error("run_id_not_found", "unknown run_id");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    const char *sp_sess = sess->project ? sess->project : "";
    if (strcmp(sp_sess, project) != 0) {
        free(project);
        char *ej = mcp_test_json_error("invalid_argument", "project does not match session");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }

    sess->last_access = time(NULL);

    const cbm_gbuf_t *gb = sess->graph;
    const cbm_gbuf_node_t **cases = NULL;
    int ncase = 0;
    if (cbm_gbuf_find_by_label(gb, "TestCase", &cases, &ncase) != 0) {
        cases = NULL;
        ncase = 0;
    }

    mcp_trace_case_t *rows = NULL;
    size_t nrows = 0;
    size_t row_cap = 0;

    for (int i = 0; i < ncase; i++) {
        const cbm_gbuf_node_t *n = cases[i];
        yyjson_doc *pdoc = yyjson_read(n->properties_json, strlen(n->properties_json), 0);
        if (!pdoc) {
            continue;
        }
        yyjson_val *proot = yyjson_doc_get_root(pdoc);
        yyjson_val *st = yyjson_obj_get(proot, "status");
        const char *ststr = st && yyjson_is_str(st) ? yyjson_get_str(st) : "passed";
        if (strcmp(ststr, "passed") == 0 || strcmp(ststr, "skipped") == 0) {
            yyjson_doc_free(pdoc);
            continue;
        }
        if (strcmp(ststr, "error") == 0 && !include_errors) {
            yyjson_doc_free(pdoc);
            continue;
        }
        if (strcmp(ststr, "failed") != 0 && strcmp(ststr, "error") != 0) {
            yyjson_doc_free(pdoc);
            continue;
        }

        char *suite = mcp_suite_for_case(gb, n->id);
        if (nrows == row_cap) {
            size_t ncap = row_cap ? row_cap * 2 : (size_t)CBM_SZ_32;
            void *nr = realloc(rows, ncap * sizeof(*rows));
            if (!nr) {
                free(suite);
                yyjson_doc_free(pdoc);
                goto oom_trace;
            }
            rows = nr;
            row_cap = ncap;
        }
        mcp_trace_case_t *row = &rows[nrows++];
        memset(row, 0, sizeof(*row));
        row->case_id = n->id;
        row->suite = suite;
        row->name = heap_strdup(n->name ? n->name : "");
        row->status = heap_strdup(ststr);
        yyjson_val *msg = yyjson_obj_get(proot, "message");
        if (msg && yyjson_is_str(msg)) {
            row->message = heap_strdup(yyjson_get_str(msg));
        }
        yyjson_val *loc = yyjson_obj_get(proot, "location");
        if (loc && yyjson_is_str(loc)) {
            row->location = heap_strdup(yyjson_get_str(loc));
        }
        yyjson_doc_free(pdoc);
    }

    qsort(rows, nrows, sizeof(*rows), mcp_trace_case_cmp);

    doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        goto oom_trace;
    }
    yyjson_mut_val *rootv = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, rootv);
    yyjson_mut_val *fail_arr = yyjson_mut_arr(doc);

    const char *calls_types[] = {"CALLS"};

    for (size_t ri = 0; ri < nrows; ri++) {
        mcp_trace_case_t *row = &rows[ri];
        yyjson_mut_val *fo = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, fo, "test_name", row->name ? row->name : "");
        yyjson_mut_obj_add_str(doc, fo, "suite", row->suite ? row->suite : "");
        yyjson_mut_obj_add_str(doc, fo, "status", row->status ? row->status : "");
        if (row->message && row->message[0]) {
            yyjson_mut_obj_add_str(doc, fo, "message", row->message);
        }
        if (row->location && row->location[0]) {
            yyjson_mut_obj_add_str(doc, fo, "location", row->location);
        }

        yyjson_mut_val *rel_arr = yyjson_mut_arr(doc);

        const cbm_gbuf_edge_t **edges = NULL;
        int ec = 0;
        if (cbm_gbuf_find_edges_by_source_type(gb, row->case_id, "TESTS", &edges, &ec) != 0) {
            edges = NULL;
            ec = 0;
        }

        for (int ei = 0; ei < ec; ei++) {
            int64_t fun_id = edges[ei]->target_id;
            const cbm_gbuf_node_t *fn = cbm_gbuf_find_by_id(gb, fun_id);
            if (!fn || !fn->qualified_name || !fn->qualified_name[0]) {
                continue;
            }
            cbm_node_t sfn = {0};
            if (cbm_store_find_node_by_qn(store, project, fn->qualified_name, &sfn) != CBM_STORE_OK) {
                continue;
            }

            cbm_traverse_result_t tr = {0};
            cbm_store_bfs(store, sfn.id, "inbound", calls_types, 1, depth, MCP_BFS_LIMIT, &tr);

            yyjson_mut_val *rf = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_str(doc, rf, "name", sfn.name ? sfn.name : "");
            yyjson_mut_obj_add_str(doc, rf, "qualified_name", sfn.qualified_name ? sfn.qualified_name : "");
            yyjson_mut_obj_add_str(doc, rf, "file", sfn.file_path ? sfn.file_path : "");

            yyjson_mut_val *carr = yyjson_mut_arr(doc);
            for (int vi = 0; vi < tr.visited_count; vi++) {
                cbm_node_t *vn = &tr.visited[vi].node;
                int hop = tr.visited[vi].hop;
                yyjson_mut_val *co = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, co, "name", vn->name ? vn->name : "");
                yyjson_mut_obj_add_str(doc, co, "file", vn->file_path ? vn->file_path : "");
                yyjson_mut_obj_add_int(doc, co, "depth", hop);
                yyjson_mut_arr_add_val(carr, co);
            }
            yyjson_mut_obj_add_val(doc, rf, "callers", carr);

            if (tr_keep_n == tr_keep_cap) {
                size_t ncap = tr_keep_cap ? tr_keep_cap * 2 : 8;
                void *nr = realloc(tr_keep, ncap * sizeof(*tr_keep));
                if (!nr) {
                    cbm_store_traverse_free(&tr);
                    cbm_node_free_fields(&sfn);
                    goto oom_trace;
                }
                tr_keep = nr;
                tr_keep_cap = ncap;
            }
            tr_keep[tr_keep_n++] = tr;

            if (sfn_keep_n == sfn_keep_cap) {
                size_t ncap = sfn_keep_cap ? sfn_keep_cap * 2 : 8;
                void *nr = realloc(sfn_keep, ncap * sizeof(*sfn_keep));
                if (!nr) {
                    tr_keep_n--;
                    cbm_store_traverse_free(&tr_keep[tr_keep_n]);
                    cbm_node_free_fields(&sfn);
                    goto oom_trace;
                }
                sfn_keep = nr;
                sfn_keep_cap = ncap;
            }
            sfn_keep[sfn_keep_n++] = sfn;

            yyjson_mut_arr_add_val(rel_arr, rf);
        }

        yyjson_mut_obj_add_val(doc, fo, "related_functions", rel_arr);
        yyjson_mut_arr_add_val(fail_arr, fo);
    }

    yyjson_mut_obj_add_val(doc, rootv, "failures", fail_arr);
    char *json = yy_doc_to_str(doc);
    yyjson_mut_doc_free(doc);
    for (size_t i = 0; i < tr_keep_n; i++) {
        cbm_store_traverse_free(&tr_keep[i]);
    }
    free(tr_keep);
    for (size_t i = 0; i < sfn_keep_n; i++) {
        cbm_node_free_fields(&sfn_keep[i]);
    }
    free(sfn_keep);
    for (size_t j = 0; j < nrows; j++) {
        mcp_trace_case_free(&rows[j]);
    }
    free(rows);
    free(project);
    return cbm_mcp_text_result(json ? json : "{}", false);

oom_trace:
    if (doc) {
        yyjson_mut_doc_free(doc);
    }
    for (size_t i = 0; i < tr_keep_n; i++) {
        cbm_store_traverse_free(&tr_keep[i]);
    }
    free(tr_keep);
    for (size_t i = 0; i < sfn_keep_n; i++) {
        cbm_node_free_fields(&sfn_keep[i]);
    }
    free(sfn_keep);
    if (rows) {
        for (size_t j = 0; j < nrows; j++) {
            mcp_trace_case_free(&rows[j]);
        }
        free(rows);
    }
    free(project);
    char *ej = mcp_test_json_error("internal", "out of memory");
    char *out_err = cbm_mcp_text_result(ej, true);
    free(ej);
    return out_err;
}

