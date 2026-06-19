/*
 * test_result.c — Canonical test run structs (heap ownership).
 */
#include "test_ingest/test_result.h"

#include <stdlib.h>
#include <string.h>

static void cbm_test_case_free_fields(cbm_test_case_t *c) {
    if (!c) {
        return;
    }
    free(c->suite);
    free(c->classname);
    free(c->name);
    free(c->message);
    free(c->type);
    free(c->location);
    free(c->stack_trace);
    memset(c, 0, sizeof(*c));
}

static void cbm_test_suite_free_contents(cbm_test_suite_t *s) {
    if (!s) {
        return;
    }
    free(s->name);
    for (size_t i = 0; i < s->case_count; i++) {
        cbm_test_case_free_fields(&s->cases[i]);
    }
    free(s->cases);
    memset(s, 0, sizeof(*s));
}

cbm_test_result_t *cbm_test_result_new(void) {
    cbm_test_result_t *r = (cbm_test_result_t *)calloc(1, sizeof(*r));
    return r;
}

void cbm_test_result_free(cbm_test_result_t *r) {
    if (!r) {
        return;
    }
    free(r->command);
    free(r->format);
    for (size_t i = 0; i < r->suite_count; i++) {
        cbm_test_suite_free_contents(&r->suites[i]);
    }
    free(r->suites);
    free(r);
}

int cbm_test_result_append_suite(cbm_test_result_t *r, const cbm_test_suite_t *suite) {
    if (!r || !suite) {
        return -1;
    }
    cbm_test_suite_t *ns =
        (cbm_test_suite_t *)realloc(r->suites, (r->suite_count + 1) * sizeof(*ns));
    if (!ns) {
        return -1;
    }
    r->suites = ns;
    r->suites[r->suite_count] = *suite;
    r->suite_count++;
    return 0;
}

int cbm_test_suite_append_case(cbm_test_suite_t *suite, const cbm_test_case_t *c) {
    if (!suite || !c) {
        return -1;
    }
    cbm_test_case_t *nc =
        (cbm_test_case_t *)realloc(suite->cases, (suite->case_count + 1) * sizeof(*nc));
    if (!nc) {
        return -1;
    }
    suite->cases = nc;
    suite->cases[suite->case_count] = *c;
    suite->case_count++;
    return 0;
}

void cbm_test_result_recompute_stats(cbm_test_result_t *r) {
    if (!r) {
        return;
    }
    r->total = 0;
    r->passed = 0;
    r->failed = 0;
    r->skipped = 0;
    r->errors = 0;
    r->duration_ms = 0;

    for (size_t si = 0; si < r->suite_count; si++) {
        cbm_test_suite_t *su = &r->suites[si];
        double suite_time = 0;
        for (size_t ci = 0; ci < su->case_count; ci++) {
            cbm_test_case_t *tc = &su->cases[ci];
            r->total++;
            suite_time += tc->duration_ms;
            switch (tc->status) {
            case CBM_TEST_STATUS_PASSED:
                r->passed++;
                break;
            case CBM_TEST_STATUS_FAILED:
                r->failed++;
                break;
            case CBM_TEST_STATUS_SKIPPED:
                r->skipped++;
                break;
            case CBM_TEST_STATUS_ERROR:
                r->errors++;
                break;
            }
        }
        if (su->duration_ms <= 0 && su->case_count > 0) {
            su->duration_ms = suite_time;
        }
        r->duration_ms += su->duration_ms;
    }
}
