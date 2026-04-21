/*
 * format_dispatch.h — Choose JUnit XML vs stdout parsers for test output ingestion.
 */
#ifndef CBM_FORMAT_DISPATCH_H
#define CBM_FORMAT_DISPATCH_H

#include "test_ingest/test_result.h"

#include <stddef.h>

/* `format`: NULL / "" / "auto" runs XML discovery first, then stdout sniffing.
 * Known formats: "junit_xml", "go_test", "pytest", "sbt".
 */
cbm_test_result_t *cbm_dispatch_format(const char *format, const char *cwd, const char *stdout_data, size_t stdout_len);

#endif /* CBM_FORMAT_DISPATCH_H */
