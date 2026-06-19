/*
 * test_graph.c — TestRun / TestSuite / TestCase graph construction.
 */
#include "test_ingest/test_graph.h"

#include <yyjson/yyjson.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *status_json(cbm_test_status_t st) {
    switch (st) {
    case CBM_TEST_STATUS_PASSED:
        return "passed";
    case CBM_TEST_STATUS_FAILED:
        return "failed";
    case CBM_TEST_STATUS_SKIPPED:
        return "skipped";
    case CBM_TEST_STATUS_ERROR:
        return "error";
    default:
        return "passed";
    }
}

static void count_suite(const cbm_test_suite_t *su, int *tot, int *pass, int *fail, int *skip, int *err) {
    *tot = *pass = *fail = *skip = *err = 0;
    for (size_t i = 0; i < su->case_count; i++) {
        (*tot)++;
        switch (su->cases[i].status) {
        case CBM_TEST_STATUS_PASSED:
            (*pass)++;
            break;
        case CBM_TEST_STATUS_FAILED:
            (*fail)++;
            break;
        case CBM_TEST_STATUS_SKIPPED:
            (*skip)++;
            break;
        case CBM_TEST_STATUS_ERROR:
            (*err)++;
            break;
        default:
            break;
        }
    }
}

static char *json_test_run(const char *run_id, const cbm_test_result_t *res, const char *project) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    if (!root) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "run_id", run_id);
    if (res->command && res->command[0]) {
        yyjson_mut_obj_add_str(doc, root, "command", res->command);
    }
    if (res->format && res->format[0]) {
        yyjson_mut_obj_add_str(doc, root, "format", res->format);
    }
    yyjson_mut_obj_add_int(doc, root, "total", res->total);
    yyjson_mut_obj_add_int(doc, root, "passed", res->passed);
    yyjson_mut_obj_add_int(doc, root, "failed", res->failed);
    yyjson_mut_obj_add_int(doc, root, "skipped", res->skipped);
    yyjson_mut_obj_add_int(doc, root, "errors", res->errors);
    yyjson_mut_obj_add_real(doc, root, "duration_ms", res->duration_ms);
    yyjson_mut_obj_add_int(doc, root, "created_at", (long long)time(NULL));
    if (project && project[0]) {
        yyjson_mut_obj_add_str(doc, root, "project", project);
    }
    size_t len = 0;
    char *out = yyjson_mut_write_opts(doc, YYJSON_WRITE_ALLOW_INVALID_UNICODE, NULL, &len, NULL);
    yyjson_mut_doc_free(doc);
    return out;
}

static char *json_test_suite(const cbm_test_suite_t *su) {
    int tot = 0, pass = 0, fail = 0, skip = 0, err = 0;
    count_suite(su, &tot, &pass, &fail, &skip, &err);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    if (!root) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "name", su->name ? su->name : "");
    yyjson_mut_obj_add_int(doc, root, "total", tot);
    yyjson_mut_obj_add_int(doc, root, "passed", pass);
    yyjson_mut_obj_add_int(doc, root, "failed", fail);
    yyjson_mut_obj_add_int(doc, root, "skipped", skip);
    yyjson_mut_obj_add_int(doc, root, "errors", err);
    yyjson_mut_obj_add_real(doc, root, "duration_ms", su->duration_ms);
    size_t len = 0;
    char *out = yyjson_mut_write_opts(doc, YYJSON_WRITE_ALLOW_INVALID_UNICODE, NULL, &len, NULL);
    yyjson_mut_doc_free(doc);
    return out;
}

static char *json_test_case(const cbm_test_case_t *tc) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    if (!root) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "status", status_json(tc->status));
    yyjson_mut_obj_add_real(doc, root, "duration_ms", tc->duration_ms);
    if (tc->message && tc->message[0]) {
        yyjson_mut_obj_add_str(doc, root, "message", tc->message);
    }
    if (tc->type && tc->type[0]) {
        yyjson_mut_obj_add_str(doc, root, "type", tc->type);
    }
    if (tc->location && tc->location[0]) {
        yyjson_mut_obj_add_str(doc, root, "location", tc->location);
    }
    if (tc->stack_trace && tc->stack_trace[0]) {
        yyjson_mut_obj_add_str(doc, root, "stack_trace", tc->stack_trace);
    }
    size_t len = 0;
    char *out = yyjson_mut_write_opts(doc, YYJSON_WRITE_ALLOW_INVALID_UNICODE, NULL, &len, NULL);
    yyjson_mut_doc_free(doc);
    return out;
}

static int upsert_tests_edge(cbm_gbuf_t *gb, cbm_store_t *store, const char *project, int64_t case_id,
                             const cbm_test_case_t *tc) {
    if (!store || !project || !project[0]) {
        return 0;
    }
    if (!tc->classname || !tc->classname[0]) {
        return 0;
    }
    cbm_node_t fn;
    memset(&fn, 0, sizeof(fn));
    if (cbm_store_find_node_by_qn(store, project, tc->classname, &fn) != CBM_STORE_OK) {
        return 0;
    }
    const char *fp = fn.file_path ? fn.file_path : "";
    const char *props = fn.properties_json ? fn.properties_json : "{}";
    int64_t fun_id = cbm_gbuf_upsert_node(gb, fn.label ? fn.label : "Function", fn.name ? fn.name : "",
                                          fn.qualified_name ? fn.qualified_name : "", fp, fn.start_line,
                                          fn.end_line, props);
    cbm_node_free_fields(&fn);
    if (fun_id == 0) {
        return -1;
    }
    if (cbm_gbuf_insert_edge(gb, case_id, fun_id, "TESTS", "{}") == 0) {
        return -1;
    }
    return 0;
}

int cbm_test_graph_build(cbm_gbuf_t *gb, cbm_store_t *store, const char *project, const char *run_id,
                         const cbm_test_result_t *res, int64_t *out_testrun_node_id) {
    if (!gb || !run_id || !run_id[0] || !res) {
        return -1;
    }
    if (out_testrun_node_id) {
        *out_testrun_node_id = 0;
    }

    char qn_run[4096];
    if (snprintf(qn_run, sizeof(qn_run), "testrun:%s", run_id) >= (int)sizeof(qn_run)) {
        return -1;
    }

    char *run_js = json_test_run(run_id, res, project);
    if (!run_js) {
        return -1;
    }
    int64_t run_id_node = cbm_gbuf_upsert_node(gb, "TestRun", run_id, qn_run, "", 0, 0, run_js);
    free(run_js);
    if (run_id_node == 0) {
        return -1;
    }
    if (out_testrun_node_id) {
        *out_testrun_node_id = run_id_node;
    }

    for (size_t si = 0; si < res->suite_count; si++) {
        const cbm_test_suite_t *su = &res->suites[si];
        if (!su->name) {
            continue;
        }
        char qn_su[4096];
        if (snprintf(qn_su, sizeof(qn_su), "testsuite:%s:%s", run_id, su->name) >= (int)sizeof(qn_su)) {
            return -1;
        }
        char *su_js = json_test_suite(su);
        if (!su_js) {
            return -1;
        }
        int64_t su_id = cbm_gbuf_upsert_node(gb, "TestSuite", su->name, qn_su, "", 0, 0, su_js);
        free(su_js);
        if (su_id == 0) {
            return -1;
        }
        if (cbm_gbuf_insert_edge(gb, run_id_node, su_id, "CONTAINS", "{}") == 0) {
            return -1;
        }

        for (size_t ci = 0; ci < su->case_count; ci++) {
            const cbm_test_case_t *tc = &su->cases[ci];
            const char *nm = tc->name ? tc->name : "";
            char qn_tc[4096];
            if (snprintf(qn_tc, sizeof(qn_tc), "testcase:%s:%s:%s", run_id, su->name, nm) >= (int)sizeof(qn_tc)) {
                return -1;
            }
            char *tc_js = json_test_case(tc);
            if (!tc_js) {
                return -1;
            }
            int64_t case_id = cbm_gbuf_upsert_node(gb, "TestCase", nm, qn_tc, "", 0, 0, tc_js);
            free(tc_js);
            if (case_id == 0) {
                return -1;
            }
            if (cbm_gbuf_insert_edge(gb, su_id, case_id, "CONTAINS", "{}") == 0) {
                return -1;
            }
            if (upsert_tests_edge(gb, store, project, case_id, tc) != 0) {
                return -1;
            }
        }
    }

    return 0;
}
