/*
 * test_result.h — Canonical in-memory test run representation (JUnit → graph builders).
 */
#ifndef CBM_TEST_RESULT_H
#define CBM_TEST_RESULT_H

#include <stddef.h>

typedef enum {
    CBM_TEST_STATUS_PASSED = 0,
    CBM_TEST_STATUS_FAILED,
    CBM_TEST_STATUS_SKIPPED,
    CBM_TEST_STATUS_ERROR
} cbm_test_status_t;

typedef struct {
    char              *suite;
    char              *classname;
    char              *name;
    cbm_test_status_t  status;
    double             duration_ms;
    char              *message;
    char              *type;
    char              *location;
    char              *stack_trace;
} cbm_test_case_t;

typedef struct {
    char            *name;
    cbm_test_case_t *cases;
    size_t           case_count;
    double           duration_ms;
} cbm_test_suite_t;

typedef struct {
    char             *command;
    char             *format;
    cbm_test_suite_t *suites;
    size_t            suite_count;
    int               total, passed, failed, skipped, errors;
    double            duration_ms;
} cbm_test_result_t;

cbm_test_result_t *cbm_test_result_new(void);
void cbm_test_result_free(cbm_test_result_t *r);

int cbm_test_result_append_suite(cbm_test_result_t *r, const cbm_test_suite_t *suite);
int cbm_test_suite_append_case(cbm_test_suite_t *suite, const cbm_test_case_t *c);

void cbm_test_result_recompute_stats(cbm_test_result_t *r);

#endif /* CBM_TEST_RESULT_H */
