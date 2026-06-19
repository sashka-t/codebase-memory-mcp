/*
 * raw_artifact.h — Persistent raw log/XML/JSON artifact store (SQLite).
 */
#ifndef CBM_RAW_ARTIFACT_H
#define CBM_RAW_ARTIFACT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct cbm_raw_store cbm_raw_store_t;

typedef struct {
    const char *project;
    const char *source;
    const char *format;
    const char *mime_type;
    const char *run_id;
    const char *tags_json;
    bool want_parse;
} cbm_raw_ingest_opts_t;

typedef struct {
    int64_t id;
    char sha256[65];
    char *source;
    char *format;
    char *mime_type;
    char *run_id;
    int64_t size_bytes;
    int64_t created_at;
    bool parse_ok;
    char *parse_diag;
    char *snippet;
} cbm_raw_artifact_meta_t;

cbm_raw_store_t *cbm_raw_store_open_default(void);
void cbm_raw_store_close(cbm_raw_store_t *store);

/* Returns 0 on success. Dedupes by (project, sha256). */
int cbm_raw_store_ingest(cbm_raw_store_t *store, const void *content, size_t len,
                         const cbm_raw_ingest_opts_t *opts, int64_t *out_id, char out_sha256[65],
                         bool *out_deduped, char **out_parse_diag);

void cbm_raw_artifact_meta_free(cbm_raw_artifact_meta_t *meta);

int cbm_raw_store_list(cbm_raw_store_t *store, const char *project, int limit, int offset,
                       cbm_raw_artifact_meta_t **out, int *out_count);

int cbm_raw_store_get(cbm_raw_store_t *store, const char *project, int64_t artifact_id,
                      const char *sha256, size_t offset, size_t max_bytes, char **out_content,
                      size_t *out_len, cbm_raw_artifact_meta_t *out_meta);

int cbm_raw_store_search(cbm_raw_store_t *store, const char *project, int64_t artifact_id,
                         const char *pattern, int limit, int offset,
                         cbm_raw_artifact_meta_t **out_hits, int *out_count);

#endif /* CBM_RAW_ARTIFACT_H */
