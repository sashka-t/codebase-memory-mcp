/* mcp_raw_tools.inc — Raw artifact MCP tools (included from mcp.c). */

#include "raw_artifact/raw_artifact.h"

static char *mcp_raw_json_error(const char *code, const char *message) {
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

static int mcp_b64_decode(const char *in, unsigned char **out, size_t *out_len) {
    static const signed char tbl[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62,
        -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,
        2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
        42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};
    if (!in || !out || !out_len) {
        return -1;
    }
    size_t in_len = strlen(in);
    size_t cap = (in_len / 4 + 2) * 3;
    unsigned char *buf = malloc(cap);
    if (!buf) {
        return -1;
    }
    size_t o = 0;
    int val = 0;
    int valb = -8;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '=') {
            break;
        }
        if (c > 255 || tbl[c] < 0) {
            continue;
        }
        val = (val << 6) + tbl[c];
        valb += 6;
        if (valb >= 0) {
            buf[o++] = (unsigned char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    *out = buf;
    *out_len = o;
    return 0;
}

static void mcp_meta_to_json(yyjson_mut_doc *doc, yyjson_mut_val *obj, const cbm_raw_artifact_meta_t *m,
                             bool include_snippet) {
    yyjson_mut_obj_add_int(doc, obj, "artifact_id", m->id);
    yyjson_mut_obj_add_strcpy(doc, obj, "sha256", m->sha256);
    if (m->source) {
        yyjson_mut_obj_add_strcpy(doc, obj, "source", m->source);
    }
    if (m->format) {
        yyjson_mut_obj_add_strcpy(doc, obj, "format", m->format);
    }
    if (m->mime_type && m->mime_type[0]) {
        yyjson_mut_obj_add_strcpy(doc, obj, "mime_type", m->mime_type);
    }
    if (m->run_id && m->run_id[0]) {
        yyjson_mut_obj_add_strcpy(doc, obj, "run_id", m->run_id);
    }
    yyjson_mut_obj_add_int(doc, obj, "size_bytes", m->size_bytes);
    yyjson_mut_obj_add_int(doc, obj, "created_at", m->created_at);
    yyjson_mut_obj_add_bool(doc, obj, "parse_ok", m->parse_ok);
    if (m->parse_diag) {
        yyjson_mut_obj_add_strcpy(doc, obj, "parse_diag", m->parse_diag);
    }
    if (include_snippet && m->snippet) {
        yyjson_mut_obj_add_strcpy(doc, obj, "snippet", m->snippet);
    }
}

static char *handle_ingest_raw_artifact(cbm_mcp_server_t *srv, const char *args) {
    if (!srv->raw_store) {
        char *ej = mcp_raw_json_error("internal", "raw artifact store unavailable");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }

    char *project = mcp_resolve_project_arg(args);
    char *content = cbm_mcp_get_string_arg(args, "content");
    char *content_b64 = cbm_mcp_get_string_arg(args, "content_base64");
    char *source = cbm_mcp_get_string_arg(args, "source");
    char *format = cbm_mcp_get_string_arg(args, "format");
    char *mime = cbm_mcp_get_string_arg(args, "mime_type");
    char *run_id = cbm_mcp_get_string_arg(args, "run_id");
    char *tags_json = cbm_mcp_get_string_arg(args, "tags");
    bool parse = cbm_mcp_get_bool_arg(args, "parse");

    if (!project) {
        free(content);
        free(content_b64);
        free(source);
        free(format);
        free(mime);
        free(run_id);
        free(tags_json);
        char *ej = mcp_raw_json_error("invalid_argument",
                                     "project or repo_path is required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }

    const void *payload = NULL;
    size_t payload_len = 0;
    unsigned char *decoded = NULL;
    if (content_b64 && content_b64[0]) {
        if (mcp_b64_decode(content_b64, &decoded, &payload_len) != 0 || !decoded) {
            free(project);
            free(content);
            free(content_b64);
            free(source);
            free(format);
            free(mime);
            free(run_id);
            free(tags_json);
            char *ej = mcp_raw_json_error("invalid_argument", "invalid content_base64");
            char *res = cbm_mcp_text_result(ej, true);
            free(ej);
            return res;
        }
        payload = decoded;
    } else if (content) {
        payload = content;
        payload_len = strlen(content);
    }

    if (!payload || payload_len == 0) {
        free(project);
        free(content);
        free(content_b64);
        free(source);
        free(format);
        free(mime);
        free(run_id);
        free(tags_json);
        free(decoded);
        char *ej = mcp_raw_json_error("invalid_argument", "content or content_base64 is required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }

    cbm_raw_ingest_opts_t opts = {
        .project = project,
        .source = source,
        .format = format,
        .mime_type = mime,
        .run_id = run_id,
        .tags_json = tags_json,
        .want_parse = parse,
    };
    int64_t artifact_id = 0;
    char sha[65];
    bool deduped = false;
    char *parse_diag = NULL;
    int rc = cbm_raw_store_ingest(srv->raw_store, payload, payload_len, &opts, &artifact_id, sha,
                                  &deduped, &parse_diag);
    free(decoded);
    free(project);
    free(content);
    free(content_b64);
    free(source);
    free(format);
    free(mime);
    free(run_id);
    free(tags_json);

    if (rc != 0) {
        free(parse_diag);
        char *ej = mcp_raw_json_error("internal", "failed to store artifact");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "artifact_id", artifact_id);
    yyjson_mut_obj_add_str(doc, root, "sha256", sha);
    yyjson_mut_obj_add_bool(doc, root, "stored", true);
    yyjson_mut_obj_add_bool(doc, root, "deduplicated", deduped);
    if (parse_diag) {
        yyjson_mut_obj_add_strcpy(doc, root, "parse_diag", parse_diag);
    }
    char *json = yy_doc_to_str(doc);
    yyjson_mut_doc_free(doc);
    free(parse_diag);
    char *res = cbm_mcp_text_result(json ? json : "{}", false);
    free(json);
    return res;
}

static char *handle_list_raw_artifacts(cbm_mcp_server_t *srv, const char *args) {
    char *project = mcp_resolve_project_arg(args);
    int limit = cbm_mcp_get_int_arg(args, "limit", 50);
    int offset = cbm_mcp_get_int_arg(args, "offset", 0);
    if (!srv->raw_store || !project) {
        free(project);
        char *ej = mcp_raw_json_error("invalid_argument", "project or repo_path is required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    cbm_raw_artifact_meta_t *items = NULL;
    int n = 0;
    if (cbm_raw_store_list(srv->raw_store, project, limit, offset, &items, &n) != 0) {
        free(project);
        char *ej = mcp_raw_json_error("internal", "list failed");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "project", project);
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (int i = 0; i < n; i++) {
        yyjson_mut_val *o = yyjson_mut_obj(doc);
        mcp_meta_to_json(doc, o, &items[i], false);
        yyjson_mut_arr_add_val(arr, o);
    }
    yyjson_mut_obj_add_val(doc, root, "artifacts", arr);
    yyjson_mut_obj_add_int(doc, root, "count", n);
    char *json = yy_doc_to_str(doc);
    yyjson_mut_doc_free(doc);
    free(project);
    for (int i = 0; i < n; i++) {
        cbm_raw_artifact_meta_free(&items[i]);
    }
    free(items);
    char *res = cbm_mcp_text_result(json ? json : "{}", false);
    free(json);
    return res;
}

static char *handle_get_raw_artifact(cbm_mcp_server_t *srv, const char *args) {
    char *project = mcp_resolve_project_arg(args);
    int64_t artifact_id = cbm_mcp_get_int_arg(args, "artifact_id", 0);
    char *sha256 = cbm_mcp_get_string_arg(args, "sha256");
    int offset = cbm_mcp_get_int_arg(args, "offset", 0);
    int max_bytes = cbm_mcp_get_int_arg(args, "max_bytes", 4096);
    if (!srv->raw_store || !project) {
        free(project);
        free(sha256);
        char *ej = mcp_raw_json_error("invalid_argument", "project or repo_path is required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    if (artifact_id <= 0 && (!sha256 || !sha256[0])) {
        free(project);
        free(sha256);
        char *ej = mcp_raw_json_error("invalid_argument", "artifact_id or sha256 is required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    char *content = NULL;
    size_t clen = 0;
    cbm_raw_artifact_meta_t meta = {0};
    if (cbm_raw_store_get(srv->raw_store, project, artifact_id, sha256, (size_t)offset,
                          (size_t)max_bytes, &content, &clen, &meta) != 0) {
        free(project);
        free(sha256);
        char *ej = mcp_raw_json_error("artifact_not_found", "artifact not found");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "project", project);
    mcp_meta_to_json(doc, root, &meta, false);
    yyjson_mut_obj_add_int(doc, root, "offset", offset);
    yyjson_mut_obj_add_int(doc, root, "returned_bytes", (int64_t)clen);
    yyjson_mut_obj_add_strcpy(doc, root, "content", content ? content : "");
    char *json = yy_doc_to_str(doc);
    yyjson_mut_doc_free(doc);
    free(project);
    free(sha256);
    free(content);
    cbm_raw_artifact_meta_free(&meta);
    char *res = cbm_mcp_text_result(json ? json : "{}", false);
    free(json);
    return res;
}

static char *handle_search_raw_artifacts(cbm_mcp_server_t *srv, const char *args) {
    char *project = mcp_resolve_project_arg(args);
    char *pattern = cbm_mcp_get_string_arg(args, "pattern");
    int64_t artifact_id = cbm_mcp_get_int_arg(args, "artifact_id", 0);
    int limit = cbm_mcp_get_int_arg(args, "limit", 20);
    int offset = cbm_mcp_get_int_arg(args, "offset", 0);
    if (!srv->raw_store || !project) {
        free(project);
        free(pattern);
        char *ej = mcp_raw_json_error("invalid_argument", "project or repo_path is required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    if (!pattern || !pattern[0]) {
        free(project);
        free(pattern);
        char *ej = mcp_raw_json_error("invalid_argument", "pattern is required");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    cbm_raw_artifact_meta_t *hits = NULL;
    int n = 0;
    if (cbm_raw_store_search(srv->raw_store, project, artifact_id, pattern, limit, offset, &hits,
                             &n) != 0) {
        free(project);
        free(pattern);
        char *ej = mcp_raw_json_error("internal", "search failed");
        char *res = cbm_mcp_text_result(ej, true);
        free(ej);
        return res;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "project", project);
    yyjson_mut_obj_add_strcpy(doc, root, "pattern", pattern);
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (int i = 0; i < n; i++) {
        yyjson_mut_val *o = yyjson_mut_obj(doc);
        mcp_meta_to_json(doc, o, &hits[i], true);
        yyjson_mut_arr_add_val(arr, o);
    }
    yyjson_mut_obj_add_val(doc, root, "hits", arr);
    yyjson_mut_obj_add_int(doc, root, "count", n);
    char *json = yy_doc_to_str(doc);
    yyjson_mut_doc_free(doc);
    free(project);
    free(pattern);
    for (int i = 0; i < n; i++) {
        cbm_raw_artifact_meta_free(&hits[i]);
    }
    free(hits);
    char *res = cbm_mcp_text_result(json ? json : "{}", false);
    free(json);
    return res;
}
