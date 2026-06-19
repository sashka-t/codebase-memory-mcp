/*
 * pytest.c — Parse pytest stdout (line-based).
 */
#include "test_ingest/pytest.h"
#include "test_ingest/test_result.h"

#include <stdlib.h>
#include <string.h>

static cbm_test_suite_t *ensure_suite(cbm_test_result_t *r, const char *name) {
    for (size_t i = 0; i < r->suite_count; i++) {
        if (r->suites[i].name && strcmp(r->suites[i].name, name) == 0) {
            return &r->suites[i];
        }
    }
    cbm_test_suite_t su = {0};
    su.name = strdup(name);
    if (!su.name) {
        return NULL;
    }
    if (cbm_test_result_append_suite(r, &su) != 0) {
        free(su.name);
        return NULL;
    }
    return &r->suites[r->suite_count - 1];
}


static cbm_test_status_t map_status(const char *tok) {
    if (!strcmp(tok, "PASSED") || !strcmp(tok, "XPASS")) {
        return CBM_TEST_STATUS_PASSED;
    }
    if (!strcmp(tok, "FAILED") || !strcmp(tok, "XFAIL")) {
        return CBM_TEST_STATUS_FAILED;
    }
    if (!strcmp(tok, "ERROR")) {
        return CBM_TEST_STATUS_ERROR;
    }
    if (!strcmp(tok, "SKIPPED") || !strcmp(tok, "SKIP")) {
        return CBM_TEST_STATUS_SKIPPED;
    }
    return CBM_TEST_STATUS_PASSED;
}

cbm_test_result_t *cbm_parse_pytest(const char *stdout_data, size_t len) {
    if (!stdout_data) {
        return NULL;
    }
    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        return NULL;
    }
    memcpy(buf, stdout_data, len);
    buf[len] = 0;

    cbm_test_result_t *r = cbm_test_result_new();
    if (!r) {
        free(buf);
        return NULL;
    }
    r->format = strdup("pytest");
    if (!r->format) {
        cbm_test_result_free(r);
        free(buf);
        return NULL;
    }

    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && buf[j] != '\n' && buf[j] != '\r' && buf[j] != '\0') {
            j++;
        }
        buf[j] = 0;
        char *line = buf + i;

        /* Summary line (optional): === 3 passed, 1 failed in 1.2s === */
        if (strstr(line, "passed") && strstr(line, "===")) {
            /* totals left to recompute */
        }

        const char *colon = strstr(line, "::");
        if (colon) {
            const char *lastsp = strrchr(line, ' ');
            if (lastsp && lastsp > colon) {
                const char *status_tok = lastsp + 1;
                cbm_test_status_t st = map_status(status_tok);
                const char *path_end = colon;
                char suite_buf[512];
                size_t slen = (size_t)(path_end - line);
                if (slen >= sizeof(suite_buf)) {
                    slen = sizeof(suite_buf) - 1;
                }
                memcpy(suite_buf, line, slen);
                suite_buf[slen] = 0;

                const char *case_start = colon + 2;
                const char *case_end = lastsp;
                while (case_end > case_start && (case_end[-1] == ' ' || case_end[-1] == '	'))
                    case_end--;
                if (case_end <= case_start)
                    continue;
                char case_buf[512];
                size_t clen = (size_t)(case_end - case_start);
                if (clen >= sizeof(case_buf)) {
                    clen = sizeof(case_buf) - 1;
                }
                memcpy(case_buf, case_start, clen);
                case_buf[clen] = 0;

                cbm_test_suite_t *su = ensure_suite(r, suite_buf);
                if (!su) {
                    cbm_test_result_free(r);
                    free(buf);
                    return NULL;
                }
                cbm_test_case_t tc = {0};
                tc.suite = strdup(su->name);
                tc.name = strdup(case_buf);
                tc.status = st;
                if (cbm_test_suite_append_case(su, &tc) != 0) {
                    cbm_test_result_free(r);
                    free(buf);
                    return NULL;
                }
            }
        }

        while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
            j++;
        }
        i = j;
    }

    cbm_test_result_recompute_stats(r);
    free(buf);
    return r;
}
