/*
 * go_test.c — Parse `go test` stdout into cbm_test_result_t.
 *
 * Recognises === RUN / --- PASS|FAIL|SKIP / ok\t<pkg> / FAIL\t<pkg> lines.
 */
#include "test_ingest/go_test.h"
#include "test_ingest/test_result.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *trim_dup(const char *p, size_t n) {
    while (n > 0 && isspace((unsigned char)p[0])) {
        p++;
        n--;
    }
    while (n > 0 && isspace((unsigned char)p[n - 1])) {
        n--;
    }
    if (n == 0) {
        return strdup("");
    }
    char *s = (char *)malloc(n + 1);
    if (!s) {
        return NULL;
    }
    memcpy(s, p, n);
    s[n] = 0;
    return s;
}

static int starts_with(const char *line, const char *pfx) {
    return strncmp(line, pfx, strlen(pfx)) == 0;
}

static cbm_test_suite_t *ensure_suite(cbm_test_result_t *r, const char *name) {
    const char *use = (name && name[0]) ? name : "unknown";
    for (size_t i = 0; i < r->suite_count; i++) {
        if (r->suites[i].name && strcmp(r->suites[i].name, use) == 0) {
            return &r->suites[i];
        }
    }
    cbm_test_suite_t su = {0};
    su.name = strdup(use);
    if (!su.name) {
        return NULL;
    }
    if (cbm_test_result_append_suite(r, &su) != 0) {
        free(su.name);
        return NULL;
    }
    return &r->suites[r->suite_count - 1];
}

static int parse_pkg_summary_line(const char *line, char **out_pkg) {
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (starts_with(p, "ok")) {
        p += 2;
    } else if (starts_with(p, "FAIL")) {
        p += 4;
    } else {
        return 0;
    }
    if (*p != 0 && *p != ' ' && *p != '\t') {
        return 0;
    }
    while (*p == ' ') {
        p++;
    }
    if (*p != '\t') {
        return 0;
    }
    p++;
    const char *start = p;
    while (*p && *p != '\t' && *p != '\r' && *p != '\n') {
        p++;
    }
    *out_pkg = trim_dup(start, (size_t)(p - start));
    return *out_pkg != NULL;
}

static double parse_duration_paren(const char *s) {
    const char *lp = strchr(s, '(');
    if (!lp) {
        return 0;
    }
    const char *rp = strchr(lp, ')');
    if (!rp) {
        return 0;
    }
    char buf[64];
    size_t n = (size_t)(rp - lp - 1);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, lp + 1, n);
    buf[n] = 0;
    char *end = NULL;
    double v = strtod(buf, &end);
    (void)end;
    return v * 1000.0;
}

typedef struct {
    char *name;
    cbm_test_status_t status;
    double duration_ms;
    char *msg;
} go_case_t;

typedef struct {
    go_case_t *items;
    size_t count;
    size_t cap;
} go_list_t;

static void go_list_clear(go_list_t *l) {
    for (size_t i = 0; i < l->count; i++) {
        free(l->items[i].name);
        free(l->items[i].msg);
    }
    free(l->items);
    memset(l, 0, sizeof(*l));
}

static int go_list_push(go_list_t *l, const go_case_t *c) {
    if (l->count + 1 > l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 8;
        go_case_t *ni = (go_case_t *)realloc(l->items, ncap * sizeof(*ni));
        if (!ni) {
            return -1;
        }
        l->items = ni;
        l->cap = ncap;
    }
    l->items[l->count++] = *c;
    return 0;
}

static int flush_cases_to_suite(cbm_test_result_t *r, const char *pkg, go_list_t *list) {
    if (!list->count) {
        return 0;
    }
    cbm_test_suite_t *su = ensure_suite(r, pkg);
    if (!su) {
        return -1;
    }
    for (size_t i = 0; i < list->count; i++) {
        cbm_test_case_t tc = {0};
        tc.suite = strdup(su->name);
        tc.name = list->items[i].name ? strdup(list->items[i].name) : strdup("");
        tc.status = list->items[i].status;
        tc.duration_ms = list->items[i].duration_ms;
        tc.message = list->items[i].msg ? strdup(list->items[i].msg) : NULL;
        list->items[i].name = NULL;
        list->items[i].msg = NULL;
        if (cbm_test_suite_append_case(su, &tc) != 0) {
            return -1;
        }
    }
    go_list_clear(list);
    return 0;
}

cbm_test_result_t *cbm_parse_go_test(const char *stdout_data, size_t len) {
    if (!stdout_data) {
        return NULL;
    }
    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        return NULL;
    }
    memcpy(buf, stdout_data, len);
    buf[len] = 0;

    cbm_test_result_t *res = cbm_test_result_new();
    if (!res) {
        free(buf);
        return NULL;
    }
    res->format = strdup("go_test");
    if (!res->format) {
        cbm_test_result_free(res);
        free(buf);
        return NULL;
    }

    go_list_t pending = {0};
    char *pending_fail_name = NULL;
    char *fail_msg = NULL;
    int in_fail = 0;

    size_t i = 0;
    while (i < len) {
        size_t j;
        char *line;
        char *pkg_line;
    reprocess:
        j = i;
        while (j < len && buf[j] != '\n' && buf[j] != '\r' && buf[j] != '\0') {
            j++;
        }
        buf[j] = 0;
        line = buf + i;

        pkg_line = NULL;
        if (parse_pkg_summary_line(line, &pkg_line)) {
            if (in_fail && pending_fail_name) {
                go_case_t gc = {0};
                gc.name = pending_fail_name;
                gc.status = CBM_TEST_STATUS_FAILED;
                gc.msg = fail_msg;
                fail_msg = NULL;
                if (go_list_push(&pending, &gc) != 0) {
                    free(pkg_line);
                    free(pending_fail_name);
                    free(fail_msg);
                    go_list_clear(&pending);
                    cbm_test_result_free(res);
                    free(buf);
                    return NULL;
                }
                pending_fail_name = NULL;
                in_fail = 0;
            }
            if (flush_cases_to_suite(res, pkg_line, &pending) != 0) {
                free(pkg_line);
                free(pending_fail_name);
                free(fail_msg);
                go_list_clear(&pending);
                cbm_test_result_free(res);
                free(buf);
                return NULL;
            }
            free(pkg_line);
            while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                j++;
            }
            i = j;
            continue;
        }

        if (in_fail && pending_fail_name) {
            const char *fp = line;
            if (*fp == '\t' || strncmp(fp, "    ", 4) == 0) {
                const char *txt = (*fp == '\t') ? fp + 1 : fp + 4;
                size_t old = fail_msg ? strlen(fail_msg) : 0;
                size_t add = strlen(txt);
                char *nm = (char *)realloc(fail_msg, old + add + 2);
                if (!nm) {
                    free(pending_fail_name);
                    free(fail_msg);
                    go_list_clear(&pending);
                    cbm_test_result_free(res);
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
            go_case_t gc = {0};
            gc.name = pending_fail_name;
            gc.status = CBM_TEST_STATUS_FAILED;
            gc.msg = fail_msg;
            fail_msg = NULL;
            if (go_list_push(&pending, &gc) != 0) {
                free(pending_fail_name);
                go_list_clear(&pending);
                cbm_test_result_free(res);
                free(buf);
                return NULL;
            }
            pending_fail_name = NULL;
            in_fail = 0;
            goto reprocess;
        }

        const char *p = line;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        if (starts_with(p, "=== RUN")) {
            p += strlen("=== RUN");
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            char *nm = trim_dup(p, strlen(p));
            free(pending_fail_name);
            pending_fail_name = nm;
            in_fail = 0;
            free(fail_msg);
            fail_msg = NULL;
            while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                j++;
            }
            i = j;
            continue;
        }

        if (starts_with(p, "--- PASS:")) {
            p += strlen("--- PASS:");
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            const char *sp = p;
            while (*sp && *sp != ' ' && *sp != '\t') {
                sp++;
            }
            char *nm = trim_dup(p, (size_t)(sp - p));
            go_case_t gc = {0};
            gc.name = nm;
            gc.status = CBM_TEST_STATUS_PASSED;
            gc.duration_ms = parse_duration_paren(line);
            if (go_list_push(&pending, &gc) != 0) {
                free(nm);
                go_list_clear(&pending);
                cbm_test_result_free(res);
                free(buf);
                return NULL;
            }
            free(pending_fail_name);
            pending_fail_name = NULL;
            while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                j++;
            }
            i = j;
            continue;
        }

        if (starts_with(p, "--- FAIL:")) {
            p += strlen("--- FAIL:");
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            const char *sp = p;
            while (*sp && *sp != ' ' && *sp != '\t') {
                sp++;
            }
            char *nm = trim_dup(p, (size_t)(sp - p));
            free(pending_fail_name);
            pending_fail_name = nm;
            in_fail = 1;
            free(fail_msg);
            fail_msg = NULL;
            while (j < len && (buf[j] == 0 || buf[j] == '\n' || buf[j] == '\r')) {
                j++;
            }
            i = j;
            continue;
        }

        if (starts_with(p, "--- SKIP:")) {
            p += strlen("--- SKIP:");
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            const char *sp = p;
            while (*sp && *sp != ' ' && *sp != '\t') {
                sp++;
            }
            char *nm = trim_dup(p, (size_t)(sp - p));
            go_case_t gc = {0};
            gc.name = nm;
            gc.status = CBM_TEST_STATUS_SKIPPED;
            gc.duration_ms = parse_duration_paren(line);
            if (go_list_push(&pending, &gc) != 0) {
                free(nm);
                go_list_clear(&pending);
                cbm_test_result_free(res);
                free(buf);
                return NULL;
            }
            free(pending_fail_name);
            pending_fail_name = NULL;
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

    if (in_fail && pending_fail_name) {
        go_case_t gc = {0};
        gc.name = pending_fail_name;
        gc.status = CBM_TEST_STATUS_FAILED;
        gc.msg = fail_msg;
        fail_msg = NULL;
        go_list_push(&pending, &gc);
        pending_fail_name = NULL;
    }
    free(pending_fail_name);
    free(fail_msg);

    if (pending.count > 0) {
        if (flush_cases_to_suite(res, "go-test", &pending) != 0) {
            go_list_clear(&pending);
            cbm_test_result_free(res);
            free(buf);
            return NULL;
        }
    }

    cbm_test_result_recompute_stats(res);
    free(buf);
    return res;
}
