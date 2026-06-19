/*
 * junit_xml.h — JUnit XML (Surefire / Gradle) → cbm_test_result_t via streaming yxml.
 */
#ifndef CBM_JUNIT_XML_H
#define CBM_JUNIT_XML_H

#include "test_ingest/test_result.h"

#include <stddef.h>

cbm_test_result_t *cbm_parse_junit_xml(const char *xml_data, size_t len);

cbm_test_result_t *cbm_parse_junit_xml_dir(const char *dir);

const char *cbm_junit_xml_last_error(void);

#endif /* CBM_JUNIT_XML_H */
