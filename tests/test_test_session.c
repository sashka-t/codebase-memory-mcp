/*
 * tests/test_test_session.c — Unit tests for test session heap objects + TTL eviction.
 */
#include "test_framework.h"
#include "test_ingest/test_session.h"
#include "graph_buffer/graph_buffer.h"

#include <string.h>
#include <time.h>

TEST(session_evict_expired_keeps_recent) {
    time_t now = (time_t)CBM_SZ_1K * (time_t)CBM_SZ_1K; /* ~1M, arbitrary fixed anchor */
    cbm_gbuf_t *gb_old = cbm_gbuf_new("proj", ".");
    ASSERT(gb_old);
    cbm_gbuf_t *gb_new = cbm_gbuf_new("proj", ".");
    ASSERT(gb_new);

    cbm_test_session_t *old = cbm_test_session_new("run_old", NULL, gb_old, 1, 1, 1, 0, 0, 0, 0.0, "pytest -q",
                                                   "junit", now);
    ASSERT(old);
    old->last_access = now - (time_t)CBM_TEST_SESSION_TTL_SECONDS - (time_t)CBM_SZ_64;

    cbm_test_session_t *recent = cbm_test_session_new("run_new", NULL, gb_new, 2, 1, 1, 0, 0, 0, 0.0, "pytest -q",
                                                       "junit", now);
    ASSERT(recent);
    recent->last_access = now - 1;

    CBMHashTable *ht = cbm_ht_create(CBM_SZ_16);
    ASSERT(ht);
    ASSERT(cbm_ht_set(ht, old->run_id, old) == NULL);
    ASSERT(cbm_ht_set(ht, recent->run_id, recent) == NULL);
    ASSERT_EQ((long long)cbm_ht_count(ht), 2);

    cbm_test_sessions_evict_expired(ht, now);
    ASSERT_EQ((long long)cbm_ht_count(ht), 1);

    void *left = cbm_ht_get(ht, "run_new");
    ASSERT(left == recent);

    void *rm = cbm_ht_delete(ht, "run_new");
    cbm_test_session_free(rm);
    cbm_ht_free(ht);
    PASS();
}

TEST(session_new_generates_run_id_when_null) {
    time_t now = time(NULL);
    cbm_gbuf_t *gb = cbm_gbuf_new("proj", ".");
    ASSERT(gb);
    cbm_test_session_t *s = cbm_test_session_new(NULL, NULL, gb, 0, 0, 0, 0, 0, 0, 0.0, NULL, NULL, now);
    ASSERT(s);
    ASSERT(s->run_id);
    ASSERT(strncmp(s->run_id, "run_", 4) == 0);
    ASSERT_EQ((long long)s->created_at, (long long)now);
    ASSERT_EQ((long long)s->last_access, (long long)now);

    cbm_test_session_free(s);
    PASS();
}

SUITE(test_session) {
    RUN_TEST(session_evict_expired_keeps_recent);
    RUN_TEST(session_new_generates_run_id_when_null);
}
