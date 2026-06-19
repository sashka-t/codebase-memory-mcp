/*
 * sbt.c — Parse sbt test stdout ([info] lines, ScalaTest-style summaries).
 */
#include "test_ingest/sbt.h"
#include "test_ingest/test_result.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *dup_slice(const char *a, const char *b) {
    size_t n = (size_t)(b - a);
    while (n && isspace((unsigned char)a[0])) {
        a++;
        n--;
    }
    while (n && isspace((unsigned char)a[n - 1])) {
        n--;
    }
    if (!n) {
        return strdup("");
    }
    char *s = (char *)malloc(n + 1);
    if (!s) {
        return NULL;
    }
    memcpy(s, a, n);
    s[n] = 0;
    return s;
}

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

static cbm_test_case_t *find_case(cbm_test_suite_t *su, const char *name) {
    for (size_t i = 0; i < su->case_count; i++) {
        if (su->cases[i].name && strcmp(su->cases[i].name, name) == 0) {
            return &su->cases[i];
        }
    }
    return NULL;
}

static const char *ltrim(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

cbm_test_result_t *cbm_parse_sbt_stdout(const char *stdout_data, size_t len) {
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
    r->format = strdup("sbt");
    if (!r->format) {
        cbm_test_result_free(r);
        free(buf);
        return NULL;
    }

    char *cur_suite = NULL;
    cbm_test_case_t *fail_target = NULL;
    char *fail_msg = NULL;

    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && buf[j] != '\n' && buf[j] != '\r' && buf[j] != '\0') {
            j++;
        }
        buf[j] = 0;
        const char *line = buf + i;

        if (strncmp(line, "[info]", 6) != 0) {
            while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                j++;
            }
            i = j;
            continue;
        }

        const char *rest = line + 6;

        const char *ts = ltrim(rest);
        if (strncmp(ts, "Tests:", 6) == 0) {
            if (fail_target && fail_msg) {
                free(fail_target->message);
                fail_target->message = fail_msg;
                fail_msg = NULL;
            }
            fail_target = NULL;
            while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                j++;
            }
            i = j;
            continue;
        }

        {
            const char *colon = strchr(rest, ':');
            if (colon && colon > rest && strstr(rest, "***") == NULL) {
                const char *after = colon + 1;
                while (*after && isspace((unsigned char)*after)) {
                    after++;
                }
                if (*after == 0) {
                    if (fail_target && fail_msg) {
                        free(fail_target->message);
                        fail_target->message = fail_msg;
                        fail_msg = NULL;
                    }
                    fail_target = NULL;
                    char *sn = dup_slice(rest, colon);
                    free(cur_suite);
                    cur_suite = sn;
                    while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                        j++;
                    }
                    i = j;
                    continue;
                }
            }
        }

        if (!cur_suite) {
            while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                j++;
            }
            i = j;
            continue;
        }

        cbm_test_suite_t *su = ensure_suite(r, cur_suite);
        if (!su) {
            free(cur_suite);
            free(fail_msg);
            cbm_test_result_free(r);
            free(buf);
            return NULL;
        }

        if (fail_target && (strncmp(rest, "    ", 4) == 0 || rest[0] == '\t')) {
            const char *txt = (rest[0] == '\t') ? rest + 1 : rest + 4;
            while (*txt == ' ' || *txt == '\t') {
                txt++;
            }
            size_t old = fail_msg ? strlen(fail_msg) : 0;
            size_t add = strlen(txt);
            char *nm = (char *)realloc(fail_msg, old + add + 2);
            if (!nm) {
                free(cur_suite);
                free(fail_msg);
                cbm_test_result_free(r);
                free(buf);
                return NULL;
            }
            fail_msg = nm;
            if (old) {
                fail_msg[old] = '\n';
                memcpy(fail_msg + old + 1, txt, add + 1);
            } else {
                memcpy(fail_msg, txt, add + 1);
            }
            while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                j++;
            }
            i = j;
            continue;
        }

        const char *cmd = ltrim(rest);
        if (strncmp(cmd, "- ", 2) == 0 && strstr(cmd, "*** FAILED ***")) {
            const char *fail = strstr(cmd, "*** FAILED ***");
            char *cn = dup_slice(cmd + 2, fail);
            cbm_test_case_t *tc = find_case(su, cn);
            free(cn);
            if (tc) {
                tc->status = CBM_TEST_STATUS_FAILED;
                fail_target = tc;
                free(fail_msg);
                fail_msg = NULL;
            }
            while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                j++;
            }
            i = j;
            continue;
        }

        if (fail_target && fail_msg) {
            free(fail_target->message);
            fail_target->message = fail_msg;
            fail_msg = NULL;
            fail_target = NULL;
        }

        if (strncmp(rest, "  ", 2) == 0 && rest[2] != '-') {
            const char *name_start = rest + 2;
            char *nm = dup_slice(name_start, name_start + strlen(name_start));
            cbm_test_case_t tc = {0};
            tc.suite = strdup(su->name);
            tc.name = nm;
            tc.status = CBM_TEST_STATUS_PASSED;
            if (cbm_test_suite_append_case(su, &tc) != 0) {
                free(nm);
                free(cur_suite);
                free(fail_msg);
                cbm_test_result_free(r);
                free(buf);
                return NULL;
            }
            while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                j++;
            }
            i = j;
            continue;
        }

        while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
            j++;
        }
        i = j;
    }

    if (fail_target && fail_msg) {
        free(fail_target->message);
        fail_target->message = fail_msg;
        fail_msg = NULL;
    }
    free(cur_suite);
    free(fail_msg);

    cbm_test_result_recompute_stats(r);
    free(buf);
    return r;
}
