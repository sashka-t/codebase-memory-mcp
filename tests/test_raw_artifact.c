/*
 * tests/test_raw_artifact.c — Raw artifact store roundtrip.
 */
#include "../src/foundation/compat.h"
#include "test_framework.h"
#include "raw_artifact/raw_artifact.h"
#include "foundation/platform.h"

#include <stdlib.h>
#include <string.h>

TEST(raw_artifact_ingest_list_get_search) {
    char *prev = getenv("CBM_CACHE_DIR");
    char cache[512];
    snprintf(cache, sizeof(cache), "/tmp/cbm_raw_test_XXXXXX");
    ASSERT(cbm_mkdtemp(cache) != NULL);
    setenv("CBM_CACHE_DIR", cache, 1);

    cbm_raw_store_t *store = cbm_raw_store_open_default();
    ASSERT(store);

    const char *payload = "line1\nERROR: boom\nline3\n";
    cbm_raw_ingest_opts_t opts = {
        .project = "proj-raw",
        .source = "unit-test",
        .format = "text",
        .want_parse = true,
    };
    int64_t id = 0;
    char sha[65];
    bool deduped = false;
    ASSERT_EQ(cbm_raw_store_ingest(store, payload, strlen(payload), &opts, &id, sha, &deduped, NULL),
              0);
    ASSERT_GT(id, 0);
    ASSERT_FALSE(deduped);

    int64_t id2 = 0;
    ASSERT_EQ(cbm_raw_store_ingest(store, payload, strlen(payload), &opts, &id2, sha, &deduped, NULL),
              0);
    ASSERT_EQ(id, id2);
    ASSERT_TRUE(deduped);

    cbm_raw_artifact_meta_t *list = NULL;
    int n = 0;
    ASSERT_EQ(cbm_raw_store_list(store, "proj-raw", 10, 0, &list, &n), 0);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(list[0].id, id);

    char *slice = NULL;
    size_t slen = 0;
    cbm_raw_artifact_meta_t meta = {0};
    ASSERT_EQ(cbm_raw_store_get(store, "proj-raw", id, NULL, 0, 64, &slice, &slen, &meta), 0);
    ASSERT(strstr(slice, "ERROR") != NULL);
    free(slice);
    cbm_raw_artifact_meta_free(&meta);

    cbm_raw_artifact_meta_t *hits = NULL;
    int nh = 0;
    ASSERT_EQ(cbm_raw_store_search(store, "proj-raw", 0, "ERROR", 5, 0, &hits, &nh), 0);
    ASSERT_EQ(nh, 1);
    ASSERT(hits[0].snippet && strstr(hits[0].snippet, "ERROR"));

    for (int i = 0; i < n; i++) {
        cbm_raw_artifact_meta_free(&list[i]);
    }
    free(list);
    cbm_raw_artifact_meta_free(&hits[0]);
    free(hits);

    cbm_raw_store_close(store);
    if (prev) {
        setenv("CBM_CACHE_DIR", prev, 1);
    } else {
        unsetenv("CBM_CACHE_DIR");
    }
    PASS();
}

SUITE(raw_artifact) {
    RUN_TEST(raw_artifact_ingest_list_get_search);
}
