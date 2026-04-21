/*
 * test_graph.h — Build TestRun / TestSuite / TestCase nodes in a graph buffer from parsed results.
 */
#ifndef CBM_TEST_GRAPH_H
#define CBM_TEST_GRAPH_H

#include "graph_buffer/graph_buffer.h"
#include "store/store.h"
#include "test_ingest/test_result.h"

#include <stdint.h>

/* Populate `gb` with test nodes and `CONTAINS` edges. When `store` and `project` are non-NULL,
 * attempts `TESTS` edges from each `TestCase` to a store `Function` resolved by `TestCase.classname`.
 * Returns 0 on success; on failure leaves `gb` unchanged and returns -1.
 */
int cbm_test_graph_build(cbm_gbuf_t *gb, cbm_store_t *store, const char *project, const char *run_id,
                         const cbm_test_result_t *res, int64_t *out_testrun_node_id);

#endif /* CBM_TEST_GRAPH_H */
