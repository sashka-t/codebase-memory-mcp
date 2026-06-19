/*
 * junit_xml.c — JUnit XML → cbm_test_result_t using vendored yxml.
 */
#include "test_ingest/junit_xml.h"

#include "foundation/compat_fs.h"
#include "test_ingest/test_result.h"
#include "yxml/yxml.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char *g_junit_last_error;

const char *cbm_junit_xml_last_error(void) {
    return g_junit_last_error ? g_junit_last_error : "";
}

static void set_err(const char *msg) {
    g_junit_last_error = msg;
}

typedef struct {
    char *s;
    size_t len;
    size_t cap;
} strbuf_t;

static void strbuf_clear(strbuf_t *b) {
    if (!b) {
        return;
    }
    b->len = 0;
    if (b->s && b->cap) {
        b->s[0] = 0;
    }
}

static void strbuf_fini(strbuf_t *b) {
    if (!b) {
        return;
    }
    free(b->s);
    b->s = NULL;
    b->len = b->cap = 0;
}

static int strbuf_append_bytes(strbuf_t *b, const char *p, size_t n) {
    if (!b || !p || n == 0) {
        return 0;
    }
    size_t need = b->len + n + 1;
    if (need > b->cap) {
        size_t ncap = b->cap ? b->cap : 64;
        while (ncap < need) {
            ncap *= 2;
        }
        char *ns = (char *)realloc(b->s, ncap);
        if (!ns) {
            return -1;
        }
        b->s = ns;
        b->cap = ncap;
    }
    memcpy(b->s + b->len, p, n);
    b->len += n;
    b->s[b->len] = 0;
    return 0;
}

static int strbuf_append_cstr(strbuf_t *b, const char *p) {
    if (!p) {
        return 0;
    }
    return strbuf_append_bytes(b, p, strlen(p));
}

typedef enum {
    ATTR_NONE = 0,
    ATTR_TESTSUITE,
    ATTR_TESTCASE,
    ATTR_FAILURE,
    ATTR_ERROR,
    ATTR_SKIPPED,
} attr_tgt_t;

typedef enum {
    CH_NONE = 0,
    CH_FAILURE,
    CH_ERROR,
} child_kind_t;


#define CBM_JUNIT_ESTACK_MAX 128

typedef struct {
    char names[CBM_JUNIT_ESTACK_MAX][96];
    int sp;
} cbm_estack_t;

static int estack_push(cbm_estack_t *st, const char *name) {
    if (!st || !name || st->sp >= CBM_JUNIT_ESTACK_MAX) {
        return -1;
    }
    snprintf(st->names[st->sp], sizeof(st->names[0]), "%s", name);
    st->sp++;
    return 0;
}

static const char *estack_pop(cbm_estack_t *st) {
    if (!st || st->sp <= 0) {
        return "";
    }
    st->sp--;
    return st->names[st->sp];
}

typedef struct {
    yxml_t yx;
    unsigned char ystack[512 * 1024];

    cbm_test_result_t *res;
    size_t suite_idx;

    cbm_test_case_t cur_case;
    int have_case;
    int in_case;

    attr_tgt_t attr_tgt;
    char attr_name[160];
    strbuf_t attr_val;
    strbuf_t body;

    int ignore_depth;
    child_kind_t ch_kind;
    cbm_estack_t est;
} junit_ctx_t;

static int flush_case(junit_ctx_t *c) {
    if (!c->have_case) {
        return 0;
    }
    if (c->suite_idx >= c->res->suite_count) {
        return -1;
    }
    cbm_test_suite_t *su = &c->res->suites[c->suite_idx];
    if (cbm_test_suite_append_case(su, &c->cur_case) != 0) {
        return -1;
    }
    memset(&c->cur_case, 0, sizeof(c->cur_case));
    c->have_case = 0;
    c->in_case = 0;
    return 0;
}

static void apply_attr(junit_ctx_t *c) {
    const char *n = c->attr_name;
    const char *v = c->attr_val.s ? c->attr_val.s : "";

    switch (c->attr_tgt) {
    case ATTR_TESTSUITE: {
        cbm_test_suite_t *su = &c->res->suites[c->suite_idx];
        if (!strcmp(n, "name")) {
            free(su->name);
            su->name = strdup(v);
        } else if (!strcmp(n, "time")) {
            su->duration_ms = strtod(v, NULL) * 1000.0;
        }
        break;
    }
    case ATTR_TESTCASE:
        if (!strcmp(n, "classname")) {
            free(c->cur_case.classname);
            c->cur_case.classname = strdup(v);
        } else if (!strcmp(n, "name")) {
            free(c->cur_case.name);
            c->cur_case.name = strdup(v);
        } else if (!strcmp(n, "time")) {
            c->cur_case.duration_ms = strtod(v, NULL) * 1000.0;
        }
        break;
    case ATTR_FAILURE:
    case ATTR_ERROR:
        if (!strcmp(n, "message")) {
            free(c->cur_case.message);
            c->cur_case.message = strdup(v);
        } else if (!strcmp(n, "type")) {
            free(c->cur_case.type);
            c->cur_case.type = strdup(v);
        }
        break;
    case ATTR_SKIPPED:
        if (!strcmp(n, "message")) {
            free(c->cur_case.message);
            c->cur_case.message = strdup(v);
        }
        break;
    default:
        break;
    }
}

static int handle_elem_start(junit_ctx_t *c) {
    const char *el = c->yx.elem;

    if (!strcmp(el, "system-out") || !strcmp(el, "system-err") || !strcmp(el, "properties") ||
        !strcmp(el, "property")) {
        c->ignore_depth = 1;
        return 0;
    }

    if (!strcmp(el, "testsuites")) {
        return 0;
    }

    if (!strcmp(el, "testsuite")) {
        cbm_test_suite_t su = {0};
        if (cbm_test_result_append_suite(c->res, &su) != 0) {
            return -1;
        }
        c->suite_idx = c->res->suite_count - 1;
        c->attr_tgt = ATTR_TESTSUITE;
        return 0;
    }

    if (!strcmp(el, "testcase")) {
        memset(&c->cur_case, 0, sizeof(c->cur_case));
        c->cur_case.status = CBM_TEST_STATUS_PASSED;
        c->have_case = 1;
        c->in_case = 1;
        c->attr_tgt = ATTR_TESTCASE;
        if (c->suite_idx < c->res->suite_count) {
            cbm_test_suite_t *su = &c->res->suites[c->suite_idx];
            if (su->name) {
                c->cur_case.suite = strdup(su->name);
            }
        }
        return 0;
    }

    if (!strcmp(el, "failure")) {
        strbuf_clear(&c->body);
        c->attr_tgt = ATTR_FAILURE;
        c->ch_kind = CH_FAILURE;
        return 0;
    }

    if (!strcmp(el, "error")) {
        strbuf_clear(&c->body);
        c->attr_tgt = ATTR_ERROR;
        c->ch_kind = CH_ERROR;
        return 0;
    }

    if (!strcmp(el, "skipped")) {
        c->attr_tgt = ATTR_SKIPPED;
        c->cur_case.status = CBM_TEST_STATUS_SKIPPED;
        return 0;
    }

    if (c->in_case) {
        /* Unknown child under <testcase> (e.g. rerunFailure) — ignore subtree. */
        c->ignore_depth = 1;
    } else {
        c->ignore_depth = 1;
    }
    return 0;
}

static int handle_elem_end(junit_ctx_t *c, const char *el) {

    if (!strcmp(el, "failure") && c->ch_kind == CH_FAILURE) {
        c->cur_case.status = CBM_TEST_STATUS_FAILED;
        if (c->body.len) {
            free(c->cur_case.stack_trace);
            c->cur_case.stack_trace = strdup(c->body.s);
        }
        strbuf_clear(&c->body);
        c->ch_kind = CH_NONE;
        c->attr_tgt = ATTR_NONE;
        return 0;
    }

    if (!strcmp(el, "error") && c->ch_kind == CH_ERROR) {
        c->cur_case.status = CBM_TEST_STATUS_ERROR;
        if (c->body.len) {
            free(c->cur_case.stack_trace);
            c->cur_case.stack_trace = strdup(c->body.s);
        }
        strbuf_clear(&c->body);
        c->ch_kind = CH_NONE;
        c->attr_tgt = ATTR_NONE;
        return 0;
    }

    if (!strcmp(el, "skipped")) {
        c->cur_case.status = CBM_TEST_STATUS_SKIPPED;
        c->attr_tgt = ATTR_NONE;
        return 0;
    }

    if (!strcmp(el, "testcase")) {
        if (flush_case(c) != 0) {
            return -1;
        }
        c->attr_tgt = ATTR_NONE;
        return 0;
    }

    c->attr_tgt = ATTR_NONE;
    return 0;
}

static void junit_ctx_fini_keep_result(junit_ctx_t *c) {
    strbuf_fini(&c->attr_val);
    strbuf_fini(&c->body);
}

cbm_test_result_t *cbm_parse_junit_xml(const char *xml_data, size_t len) {
    g_junit_last_error = NULL;
    if (!xml_data || len == 0) {
        set_err("empty_input");
        return NULL;
    }

    junit_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.suite_idx = (size_t)-1;
    c.res = cbm_test_result_new();
    if (!c.res) {
        set_err("oom");
        return NULL;
    }
    c.res->format = strdup("junit_xml");
    if (!c.res->format) {
        cbm_test_result_free(c.res);
        set_err("oom");
        return NULL;
    }

    yxml_init(&c.yx, c.ystack, sizeof(c.ystack));

    for (size_t i = 0; i < len; i++) {
        yxml_ret_t r = yxml_parse(&c.yx, (unsigned char)xml_data[i]);
        if (r < 0) {
            junit_ctx_fini_keep_result(&c);
            cbm_test_result_free(c.res);
            set_err("malformed_xml");
            return NULL;
        }

        switch (r) {
        case YXML_ELEMSTART:
            if (estack_push(&c.est, c.yx.elem) != 0) {
                junit_ctx_fini_keep_result(&c);
                cbm_test_result_free(c.res);
                set_err("internal");
                return NULL;
            }
            if (c.ignore_depth > 0) {
                c.ignore_depth++;
                break;
            }
            if (handle_elem_start(&c) != 0) {
                junit_ctx_fini_keep_result(&c);
                cbm_test_result_free(c.res);
                set_err("oom");
                return NULL;
            }
            break;
        case YXML_ELEMEND: {
            const char *closed = estack_pop(&c.est);
            if (c.ignore_depth > 0) {
                c.ignore_depth--;
                break;
            }
            int rc = handle_elem_end(&c, closed);
            if (rc != 0) {
                junit_ctx_fini_keep_result(&c);
                cbm_test_result_free(c.res);
                set_err("internal");
                return NULL;
            }
            break;
        }
        case YXML_ATTRSTART:
            strbuf_clear(&c.attr_val);
            snprintf(c.attr_name, sizeof(c.attr_name), "%s", c.yx.attr);
            break;
        case YXML_ATTRVAL:
            strbuf_append_cstr(&c.attr_val, c.yx.data);
            break;
        case YXML_ATTREND:
            apply_attr(&c);
            strbuf_clear(&c.attr_val);
            break;
        case YXML_CONTENT:
            if (c.ch_kind == CH_FAILURE || c.ch_kind == CH_ERROR) {
                strbuf_append_cstr(&c.body, c.yx.data);
            }
            break;
        default:
            break;
        }
    }

    if (c.have_case && flush_case(&c) != 0) {
        junit_ctx_fini_keep_result(&c);
        cbm_test_result_free(c.res);
        set_err("internal");
        return NULL;
    }

    if (yxml_eof(&c.yx) < 0) {
        junit_ctx_fini_keep_result(&c);
        cbm_test_result_free(c.res);
        set_err("malformed_xml");
        return NULL;
    }

    junit_ctx_fini_keep_result(&c);
    cbm_test_result_recompute_stats(c.res);
    return c.res;
}

static char *read_entire_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    if (out_len) {
        *out_len = rd;
    }
    return buf;
}

static int cmp_str(const void *a, const void *b) {
    char *const *pa = (char *const *)a;
    char *const *pb = (char *const *)b;
    return strcmp(*pa, *pb);
}

cbm_test_result_t *cbm_parse_junit_xml_dir(const char *dir) {
    g_junit_last_error = NULL;
    if (!dir || !dir[0]) {
        set_err("bad_dir");
        return NULL;
    }

    cbm_test_result_t *acc = cbm_test_result_new();
    if (!acc) {
        set_err("oom");
        return NULL;
    }
    acc->format = strdup("junit_xml");
    if (!acc->format) {
        cbm_test_result_free(acc);
        set_err("oom");
        return NULL;
    }

    cbm_dir_t *d = cbm_opendir(dir);
    if (!d) {
        cbm_test_result_free(acc);
        set_err("opendir_failed");
        return NULL;
    }

    char **names = NULL;
    size_t ncount = 0;

    for (cbm_dirent_t *e = cbm_readdir(d); e; e = cbm_readdir(d)) {
        if (e->name[0] == '.') {
            continue;
        }
        size_t nl = strlen(e->name);
        if (nl < 5 || strcmp(e->name + nl - 4, ".xml") != 0) {
            continue;
        }
        char **nn = (char **)realloc(names, (ncount + 1) * sizeof(*nn));
        if (!nn) {
            free(names);
            cbm_closedir(d);
            cbm_test_result_free(acc);
            set_err("oom");
            return NULL;
        }
        names = nn;
        names[ncount] = strdup(e->name);
        if (!names[ncount]) {
            for (size_t j = 0; j < ncount; j++) {
                free(names[j]);
            }
            free(names);
            cbm_closedir(d);
            cbm_test_result_free(acc);
            set_err("oom");
            return NULL;
        }
        ncount++;
    }
    cbm_closedir(d);

    if (ncount == 0) {
        free(names);
        cbm_test_result_recompute_stats(acc);
        return acc;
    }

    qsort(names, ncount, sizeof(names[0]), cmp_str);

    for (size_t i = 0; i < ncount; i++) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        free(names[i]);

        size_t len = 0;
        char *xml = read_entire_file(path, &len);
        if (!xml) {
            continue;
        }
        cbm_test_result_t *part = cbm_parse_junit_xml(xml, len);
        free(xml);
        if (!part) {
            free(names);
            cbm_test_result_free(acc);
            return NULL;
        }
        for (size_t si = 0; si < part->suite_count; si++) {
            cbm_test_suite_t su = part->suites[si];
            memset(&part->suites[si], 0, sizeof(part->suites[si]));
            if (cbm_test_result_append_suite(acc, &su) != 0) {
                cbm_test_result_free(part);
                free(names);
                cbm_test_result_free(acc);
                set_err("oom");
                return NULL;
            }
        }
        free(part->suites);
        part->suites = NULL;
        part->suite_count = 0;
        free(part->format);
        free(part->command);
        free(part);
    }
    free(names);

    cbm_test_result_recompute_stats(acc);
    return acc;
}
