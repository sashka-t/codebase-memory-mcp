/*
 * pytest.h — Parse pytest stdout into cbm_test_result_t.
 */
#ifndef CBM_PYTEST_H
#define CBM_PYTEST_H

#include "test_ingest/test_result.h"

#include <stddef.h>

cbm_test_result_t *cbm_parse_pytest(const char *stdout_data, size_t len);

#endif /* CBM_PYTEST_H */
