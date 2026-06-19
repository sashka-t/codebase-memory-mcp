/*
 * tests/test_test_graph.c — Unit tests for test result → graph buffer builder.
 */
#include "test_framework.h"
#include "test_ingest/test_graph.h"
#include "test_ingest/test_result.h"
#include "graph_buffer/graph_buffer.h"
#include "store/store.h"

#include <stdlib.h>
#include <string.h>

static cbm_test_result_t *make_simple_result(void) {
    cbm_test_result_t *r = cbm_test_result_new();
    if (!r) {
        return NULL;
    }
    r->format = strdup("pytest");
    r->command = strdup("pytest -q");
    cbm_test_suite_t su = {0};
    su.name = strdup("suiteA");
    su.duration_ms = 0;
    cbm_test_case_t tc = {0};
    tc.suite = strdup("suiteA");
    tc.name = strdup("case1");
    tc.status = CBM_TEST_STATUS_PASSED;
    tc.duration_ms = 12.0;
    if (cbm_test_suite_append_case(&su, &tc) != 0) {
        cbm_test_result_free(r);
        return NULL;
    }
    if (cbm_test_result_append_suite(r, &su) != 0) {
        cbm_test_result_free(r);
        return NULL;
    }
    cbm_test_result_recompute_stats(r);
    return r;
}

TEST(graph_build_counts_contains_edges) {
    cbm_test_result_t *r = make_simple_result();
    cbm_gbuf_t *gb = cbm_gbuf_new("proj", ".");
    ASSERT(gb);
    int64_t run_nid = 0;
    ASSERT(cbm_test_graph_build(gb, NULL, NULL, "run1", r, &run_nid) == 0);
    ASSERT(run_nid > 0);

    const cbm_gbuf_node_t *run = cbm_gbuf_find_by_id(gb, run_nid);
    ASSERT(run);
    ASSERT_STR_EQ(run->label, "TestRun");

    const cbm_gbuf_node_t **by_label = NULL;
    int nlab = 0;
    ASSERT(cbm_gbuf_find_by_label(gb, "TestSuite", &by_label, &nlab) == 0);
    ASSERT_EQ((long long)nlab, 1);
    ASSERT_STR_EQ(by_label[0]->label, "TestSuite");

    ASSERT(cbm_gbuf_find_by_label(gb, "TestCase", &by_label, &nlab) == 0);
    ASSERT_EQ((long long)nlab, 1);
    ASSERT_STR_EQ(by_label[0]->label, "TestCase");

    ASSERT_EQ((long long)cbm_gbuf_edge_count_by_type(gb, "CONTAINS"), 2);
    ASSERT_EQ((long long)cbm_gbuf_edge_count_by_type(gb, "TESTS"), 0);

    cbm_gbuf_free(gb);
    cbm_test_result_free(r);
    PASS();
}

TEST(graph_build_tests_edge_from_store) {
    cbm_test_result_t *r = make_simple_result();
    r->suites[0].cases[0].classname = strdup("demo.fn.Target");

    cbm_store_t *s = cbm_store_open_memory();
    ASSERT(s);
    ASSERT(cbm_store_upsert_project(s, "demo", "/tmp/demo") == CBM_STORE_OK);
    cbm_node_t fn = {0};
    fn.project = "demo";
    fn.label = "Function";
    fn.name = "Target";
    fn.qualified_name = "demo.fn.Target";
    fn.file_path = "t.go";
    fn.start_line = 10;
    fn.end_line = 20;
    fn.properties_json = "{}";
    ASSERT(cbm_store_upsert_node(s, &fn) > 0);

    cbm_gbuf_t *gb = cbm_gbuf_new("demo", ".");
    ASSERT(gb);
    int64_t run_nid = 0;
    ASSERT(cbm_test_graph_build(gb, s, "demo", "run2", r, &run_nid) == 0);
    ASSERT_EQ((long long)cbm_gbuf_edge_count_by_type(gb, "TESTS"), 1);

    cbm_gbuf_free(gb);
    cbm_store_close(s);
    cbm_test_result_free(r);
    PASS();
}

SUITE(test_graph) {
    RUN_TEST(graph_build_counts_contains_edges);
    RUN_TEST(graph_build_tests_edge_from_store);
}
