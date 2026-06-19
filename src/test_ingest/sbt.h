/*
 * sbt.h — Parse sbt test stdout into cbm_test_result_t.
 *
 * Prefer JUnit XML from target/test-reports/ when available — handled by format_dispatch (Task 4).
 */
#ifndef CBM_SBT_H
#define CBM_SBT_H

#include "test_ingest/test_result.h"

#include <stddef.h>

cbm_test_result_t *cbm_parse_sbt_stdout(const char *stdout_data, size_t len);

#endif /* CBM_SBT_H */
