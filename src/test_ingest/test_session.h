/*
 * test_session.h — Ephemeral test-run sessions keyed by run_id (TTL eviction).
 */
#ifndef CBM_TEST_SESSION_H
#define CBM_TEST_SESSION_H

#include "foundation/constants.h"
#include "foundation/hash_table.h"

#include <stdint.h>
#include <time.h>

typedef struct cbm_gbuf cbm_gbuf_t;

#define CBM_TEST_SESSION_TTL_SECONDS 1800

typedef struct cbm_test_session {
    char *run_id;
    char *project; /* may be NULL */
    cbm_gbuf_t *graph;
    int64_t test_run_node_id;
    int total;
    int passed;
    int failed;
    int skipped;
    int errors;
    double duration_ms;
    char command[CBM_SZ_512];
    char format[CBM_SZ_64];
    time_t created_at;
    time_t last_access;
} cbm_test_session_t;

/* Transfers ownership of `graph`. When `run_id` is NULL or empty, generates
 * `run_<time>_<rand4>` via snprintf per spec. `project`, `command`, and `format`
 * may be NULL. Sets `created_at` and `last_access` to `now`. */
cbm_test_session_t *cbm_test_session_new(const char *run_id, const char *project, cbm_gbuf_t *graph,
                                        int64_t test_run_node_id, int total, int passed, int failed,
                                        int skipped, int errors, double duration_ms,
                                        const char *command, const char *format, time_t now);

void cbm_test_session_free(void *session);

/* Removes entries whose `last_access` is older than `CBM_TEST_SESSION_TTL_SECONDS`
 * relative to `now`. Frees each removed `cbm_test_session_t`. */
void cbm_test_sessions_evict_expired(CBMHashTable *ht, time_t now);

#endif /* CBM_TEST_SESSION_H */
