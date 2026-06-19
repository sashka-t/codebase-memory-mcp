/*
 * raw_artifact.c — SQLite-backed raw artifact persistence.
 */
#include "raw_artifact/raw_artifact.h"

#include "foundation/constants.h"
#include "foundation/log.h"
#include "foundation/platform.h"
#include "foundation/sha256.h"
#include "foundation/str_util.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { RAW_INLINE_MAX = 16 * 1024 * 1024 };

struct cbm_raw_store {
    sqlite3 *db;
};

static int exec_sql(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        cbm_log_error("raw_artifact.schema", "err", err ? err : sqlite3_errmsg(db));
        sqlite3_free(err);
    }
    return rc == SQLITE_OK ? 0 : -1;
}

static int init_schema(sqlite3 *db) {
    return exec_sql(db,
                    "CREATE TABLE IF NOT EXISTS raw_artifacts ("
                    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "  project TEXT NOT NULL,"
                    "  sha256 TEXT NOT NULL,"
                    "  source TEXT,"
                    "  format TEXT,"
                    "  mime_type TEXT,"
                    "  run_id TEXT,"
                    "  tags_json TEXT,"
                    "  created_at INTEGER NOT NULL,"
                    "  size_bytes INTEGER NOT NULL,"
                    "  content BLOB NOT NULL,"
                    "  parse_ok INTEGER,"
                    "  parse_diag TEXT,"
                    "  UNIQUE(project, sha256)"
                    ");"
                    "CREATE INDEX IF NOT EXISTS idx_raw_project_id ON raw_artifacts(project, id DESC);");
}

cbm_raw_store_t *cbm_raw_store_open_default(void) {
    char path[CBM_SZ_1K];
    const char *dir = cbm_resolve_cache_dir();
    if (!dir) {
        return NULL;
    }
    snprintf(path, sizeof(path), "%s/_artifacts.db", dir);

    cbm_raw_store_t *s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    if (sqlite3_open_v2(path, &s->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        cbm_log_error("raw_artifact.open", "path", path, "err", sqlite3_errmsg(s->db));
        sqlite3_close(s->db);
        free(s);
        return NULL;
    }
    if (init_schema(s->db) != 0) {
        cbm_raw_store_close(s);
        return NULL;
    }
    return s;
}

void cbm_raw_store_close(cbm_raw_store_t *store) {
    if (!store) {
        return;
    }
    if (store->db) {
        sqlite3_close(store->db);
    }
    free(store);
}

void cbm_raw_artifact_meta_free(cbm_raw_artifact_meta_t *meta) {
    if (!meta) {
        return;
    }
    free(meta->source);
    free(meta->format);
    free(meta->mime_type);
    free(meta->run_id);
    free(meta->parse_diag);
    free(meta->snippet);
    memset(meta, 0, sizeof(*meta));
}

static char *dup_col_text(sqlite3_stmt *stmt, int col) {
    const unsigned char *t = sqlite3_column_text(stmt, col);
    return t ? strdup((const char *)t) : NULL;
}

static int fill_meta_from_stmt(sqlite3_stmt *stmt, cbm_raw_artifact_meta_t *meta) {
    memset(meta, 0, sizeof(*meta));
    meta->id = sqlite3_column_int64(stmt, 0);
    const unsigned char *sha = sqlite3_column_text(stmt, 1);
    if (sha) {
        snprintf(meta->sha256, sizeof(meta->sha256), "%s", (const char *)sha);
    }
    meta->source = dup_col_text(stmt, 2);
    meta->format = dup_col_text(stmt, 3);
    meta->mime_type = dup_col_text(stmt, 4);
    meta->run_id = dup_col_text(stmt, 5);
    meta->size_bytes = sqlite3_column_int64(stmt, 6);
    meta->created_at = sqlite3_column_int64(stmt, 7);
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
        meta->parse_ok = sqlite3_column_int(stmt, 8) != 0;
    }
    meta->parse_diag = dup_col_text(stmt, 9);
    return 0;
}

static bool content_sniff(const void *content, size_t len, const char *needle) {
    char tmp[4097];
    size_t n = len < 4096 ? len : 4096;
    memcpy(tmp, content, n);
    tmp[n] = '\0';
    return strstr(tmp, needle) != NULL;
}

static char *best_effort_parse_diag(const char *format, const void *content, size_t len) {
    if (!content || len == 0) {
        return strdup("empty content");
    }
    const char *fmt = (format && format[0]) ? format : "auto";
    if (strcmp(fmt, "junit_xml") == 0 ||
        (strcmp(fmt, "auto") == 0 && content_sniff(content, len, "<testsuite"))) {
        return strdup("junit_xml: sniffed; use ingest_test_reports for structured test graph");
    }
    if (strstr(fmt, "json") != NULL ||
        (strcmp(fmt, "auto") == 0 &&
         (((const char *)content)[0] == '{' || ((const char *)content)[0] == '['))) {
        return strdup("json: stored; structured parse not required for retrieval");
    }
    return strdup("stored");
}

int cbm_raw_store_ingest(cbm_raw_store_t *store, const void *content, size_t len,
                         const cbm_raw_ingest_opts_t *opts, int64_t *out_id, char out_sha256[65],
                         bool *out_deduped, char **out_parse_diag) {
    if (!store || !store->db || !opts || !opts->project || !opts->project[0] || !content ||
        len == 0 || len > RAW_INLINE_MAX) {
        return -1;
    }
    if (!cbm_validate_project_name(opts->project)) {
        return -1;
    }

    cbm_sha256_hex((const unsigned char *)content, len, out_sha256);

    sqlite3_stmt *find = NULL;
    if (sqlite3_prepare_v2(store->db,
                           "SELECT id FROM raw_artifacts WHERE project=?1 AND sha256=?2 LIMIT 1", -1,
                           &find, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(find, 1, opts->project, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(find, 2, out_sha256, -1, SQLITE_TRANSIENT);
    int rc_find = sqlite3_step(find);
    if (rc_find == SQLITE_ROW) {
        if (out_id) {
            *out_id = sqlite3_column_int64(find, 0);
        }
        if (out_deduped) {
            *out_deduped = true;
        }
        sqlite3_finalize(find);
        if (out_parse_diag) {
            *out_parse_diag = strdup("deduplicated existing artifact with same sha256");
        }
        return 0;
    }
    sqlite3_finalize(find);

    char *parse_diag = NULL;
    bool parse_ok = true;
    if (opts->want_parse) {
        parse_diag = best_effort_parse_diag(opts->format, content, len);
        parse_ok = parse_diag != NULL;
    }

    sqlite3_stmt *ins = NULL;
    if (sqlite3_prepare_v2(store->db,
                           "INSERT INTO raw_artifacts(project,sha256,source,format,mime_type,run_id,"
                           "tags_json,created_at,size_bytes,content,parse_ok,parse_diag) "
                           "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12)",
                           -1, &ins, NULL) != SQLITE_OK) {
        free(parse_diag);
        return -1;
    }
    time_t now = time(NULL);
    sqlite3_bind_text(ins, 1, opts->project, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 2, out_sha256, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 3, opts->source ? opts->source : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 4, opts->format ? opts->format : "auto", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 5, opts->mime_type ? opts->mime_type : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 6, opts->run_id ? opts->run_id : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 7, opts->tags_json ? opts->tags_json : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(ins, 8, (sqlite3_int64)now);
    sqlite3_bind_int64(ins, 9, (sqlite3_int64)len);
    sqlite3_bind_blob(ins, 10, content, (int)len, SQLITE_TRANSIENT);
    sqlite3_bind_int(ins, 11, parse_ok ? 1 : 0);
    sqlite3_bind_text(ins, 12, parse_diag ? parse_diag : "", -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(ins);
    sqlite3_finalize(ins);
    if (rc != SQLITE_DONE) {
        free(parse_diag);
        return -1;
    }
    if (out_id) {
        *out_id = sqlite3_last_insert_rowid(store->db);
    }
    if (out_deduped) {
        *out_deduped = false;
    }
    if (out_parse_diag) {
        *out_parse_diag = parse_diag;
    } else {
        free(parse_diag);
    }
    return 0;
}

int cbm_raw_store_list(cbm_raw_store_t *store, const char *project, int limit, int offset,
                       cbm_raw_artifact_meta_t **out, int *out_count) {
    if (!store || !project || !out || !out_count) {
        return -1;
    }
    *out = NULL;
    *out_count = 0;
    if (limit <= 0) {
        limit = 50;
    }
    if (offset < 0) {
        offset = 0;
    }

    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT id,sha256,source,format,mime_type,run_id,size_bytes,created_at,parse_ok,parse_diag "
        "FROM raw_artifacts WHERE project=?1 ORDER BY id DESC LIMIT ?2 OFFSET ?3";
    if (sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, project, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    sqlite3_bind_int(stmt, 3, offset);

    int cap = 0;
    cbm_raw_artifact_meta_t *arr = NULL;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (*out_count >= cap) {
            int ncap = cap ? cap * 2 : 8;
            void *p = realloc(arr, (size_t)ncap * sizeof(*arr));
            if (!p) {
                sqlite3_finalize(stmt);
                for (int i = 0; i < *out_count; i++) {
                    cbm_raw_artifact_meta_free(&arr[i]);
                }
                free(arr);
                return -1;
            }
            arr = p;
            cap = ncap;
        }
        fill_meta_from_stmt(stmt, &arr[*out_count]);
        (*out_count)++;
    }
    sqlite3_finalize(stmt);
    *out = arr;
    return 0;
}

int cbm_raw_store_get(cbm_raw_store_t *store, const char *project, int64_t artifact_id,
                      const char *sha256, size_t offset, size_t max_bytes, char **out_content,
                      size_t *out_len, cbm_raw_artifact_meta_t *out_meta) {
    if (!store || !project) {
        return -1;
    }
    if (artifact_id <= 0 && (!sha256 || !sha256[0])) {
        return -1;
    }
    if (max_bytes == 0) {
        max_bytes = 4096;
    }
    if (max_bytes > RAW_INLINE_MAX) {
        max_bytes = RAW_INLINE_MAX;
    }

    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT id,sha256,source,format,mime_type,run_id,size_bytes,created_at,parse_ok,parse_diag,"
        "substr(content, ?4, ?5) "
        "FROM raw_artifacts WHERE project=?1 AND "
        "((?2 > 0 AND id=?2) OR (?3 != '' AND sha256=?3)) LIMIT 1";
    if (sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, project, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, artifact_id);
    sqlite3_bind_text(stmt, 3, sha256 ? sha256 : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)(offset + 1));
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)max_bytes);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    if (out_meta) {
        fill_meta_from_stmt(stmt, out_meta);
    }
    const void *blob = sqlite3_column_blob(stmt, 10);
    int blen = sqlite3_column_bytes(stmt, 10);
    if (out_content && blen > 0 && blob) {
        *out_content = malloc((size_t)blen + 1);
        if (!*out_content) {
            sqlite3_finalize(stmt);
            return -1;
        }
        memcpy(*out_content, blob, (size_t)blen);
        (*out_content)[blen] = '\0';
        if (out_len) {
            *out_len = (size_t)blen;
        }
    } else if (out_content) {
        *out_content = strdup("");
        if (out_len) {
            *out_len = 0;
        }
    }
    sqlite3_finalize(stmt);
    return 0;
}

int cbm_raw_store_search(cbm_raw_store_t *store, const char *project, int64_t artifact_id,
                         const char *pattern, int limit, int offset,
                         cbm_raw_artifact_meta_t **out_hits, int *out_count) {
    if (!store || !project || !pattern || !pattern[0] || !out_hits || !out_count) {
        return -1;
    }
    *out_hits = NULL;
    *out_count = 0;
    if (limit <= 0) {
        limit = 20;
    }

    sqlite3_stmt *stmt = NULL;
    const char *sql =
        artifact_id > 0
            ? "SELECT id,sha256,source,format,mime_type,run_id,size_bytes,created_at,parse_ok,"
              "parse_diag, substr(content,1,512) "
              "FROM raw_artifacts WHERE project=?1 AND id=?2 AND content LIKE ?3 "
              "ORDER BY id DESC LIMIT ?4 OFFSET ?5"
            : "SELECT id,sha256,source,format,mime_type,run_id,size_bytes,created_at,parse_ok,"
              "parse_diag, substr(content,1,512) "
              "FROM raw_artifacts WHERE project=?1 AND content LIKE ?2 "
              "ORDER BY id DESC LIMIT ?3 OFFSET ?4";
    if (sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    char like[CBM_SZ_512];
    snprintf(like, sizeof(like), "%%%s%%", pattern);

    sqlite3_bind_text(stmt, 1, project, -1, SQLITE_TRANSIENT);
    if (artifact_id > 0) {
        sqlite3_bind_int64(stmt, 2, artifact_id);
        sqlite3_bind_text(stmt, 3, like, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, limit);
        sqlite3_bind_int(stmt, 5, offset);
    } else {
        sqlite3_bind_text(stmt, 2, like, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, limit);
        sqlite3_bind_int(stmt, 4, offset);
    }

    int cap = 0;
    cbm_raw_artifact_meta_t *arr = NULL;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (*out_count >= cap) {
            int ncap = cap ? cap * 2 : 8;
            void *p = realloc(arr, (size_t)ncap * sizeof(*arr));
            if (!p) {
                break;
            }
            arr = p;
            cap = ncap;
        }
        fill_meta_from_stmt(stmt, &arr[*out_count]);
        arr[*out_count].snippet = dup_col_text(stmt, 10);
        (*out_count)++;
    }
    sqlite3_finalize(stmt);
    *out_hits = arr;
    return 0;
}
