/*
 * go_test.h — Parse `go test` stdout into cbm_test_result_t.
 */
#ifndef CBM_GO_TEST_H
#define CBM_GO_TEST_H

#include "test_ingest/test_result.h"

#include <stddef.h>

cbm_test_result_t *cbm_parse_go_test(const char *stdout_data, size_t len);

#endif /* CBM_GO_TEST_H */
