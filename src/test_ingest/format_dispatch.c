/*
 * format_dispatch.c — Dispatch between JUnit XML directories and stdout parsers.
 */
#include "test_ingest/format_dispatch.h"
#include "test_ingest/test_result.h"
#include "test_ingest/report_scan.h"
#include "test_ingest/junit_xml.h"
#include "test_ingest/go_test.h"
#include "test_ingest/pytest.h"
#include "test_ingest/sbt.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *dir_parent_dup(const char *path) {
    const char *sl = strrchr(path, '/');
    if (!sl || sl == path) {
        return strdup(".");
    }
    size_t n = (size_t)(sl - path);
    char *d = (char *)malloc(n + 1);
    if (!d) {
        return NULL;
    }
    memcpy(d, path, n);
    d[n] = 0;
    return d;
}

static int dir_in_list(char **dirs, int nd, const char *d) {
    for (int i = 0; i < nd; i++) {
        if (strcmp(dirs[i], d) == 0) {
            return 1;
        }
    }
    return 0;
}

static int collect_unique_dirs_from_files(char **files, int nfile, char ***out_dirs, int *out_nd) {
    char **dirs = NULL;
    int nd = 0;
    for (int i = 0; i < nfile; i++) {
        char *d = dir_parent_dup(files[i]);
        if (!d) {
            goto fail;
        }
        if (!dir_in_list(dirs, nd, d)) {
            char **nn = (char **)realloc(dirs, (size_t)(nd + 1) * sizeof(*nn));
            if (!nn) {
                free(d);
                goto fail;
            }
            dirs = nn;
            dirs[nd++] = d;
        } else {
            free(d);
        }
    }
    *out_dirs = dirs;
    *out_nd = nd;
    return 0;
fail:
    for (int j = 0; j < nd; j++) {
        free(dirs[j]);
    }
    free(dirs);
    return -1;
}

static int merge_part(cbm_test_result_t *acc, cbm_test_result_t *part) {
    for (size_t si = 0; si < part->suite_count; si++) {
        cbm_test_suite_t su = part->suites[si];
        memset(&part->suites[si], 0, sizeof(part->suites[si]));
        if (cbm_test_result_append_suite(acc, &su) != 0) {
            return -1;
        }
    }
    free(part->suites);
    part->suites = NULL;
    part->suite_count = 0;
    free(part->format);
    free(part->command);
    free(part);
    return 0;
}

static cbm_test_result_t *from_junit_dirs(char **dirs, int nd) {
    cbm_test_result_t *acc = cbm_test_result_new();
    if (!acc) {
        return NULL;
    }
    acc->format = strdup("junit_xml");
    if (!acc->format) {
        cbm_test_result_free(acc);
        return NULL;
    }
    for (int i = 0; i < nd; i++) {
        cbm_test_result_t *part = cbm_parse_junit_xml_dir(dirs[i]);
        if (!part) {
            cbm_test_result_free(acc);
            return NULL;
        }
        if (merge_part(acc, part) != 0) {
            cbm_test_result_free(acc);
            return NULL;
        }
    }
    cbm_test_result_recompute_stats(acc);
    return acc;
}

static cbm_test_result_t *from_stdout_sniff(const char *stdout_data, size_t stdout_len) {
    if (!stdout_data || stdout_len == 0) {
        return NULL;
    }
    char *z = (char *)malloc(stdout_len + 1);
    if (!z) {
        return NULL;
    }
    memcpy(z, stdout_data, stdout_len);
    z[stdout_len] = 0;

    cbm_test_result_t *r = NULL;
    if (strstr(z, "=== RUN ")) {
        r = cbm_parse_go_test(z, stdout_len);
    } else if (strstr(z, "[info]")) {
        r = cbm_parse_sbt_stdout(z, stdout_len);
    } else {
        r = cbm_parse_pytest(z, stdout_len);
    }
    free(z);
    return r;
}

cbm_test_result_t *cbm_dispatch_format(const char *format, const char *cwd, const char *stdout_data, size_t stdout_len) {
    const char *fmt = (format && format[0]) ? format : "auto";

    if (strcmp(fmt, "auto") == 0) {
        char **files = NULL;
        int nfile = 0;
        int sres = (cwd && cwd[0]) ? cbm_scan_report_dirs(cwd, &files, &nfile) : -1;
        if (sres == 0 && nfile > 0) {
            char **dirs = NULL;
            int nd = 0;
            if (collect_unique_dirs_from_files(files, nfile, &dirs, &nd) != 0) {
                cbm_scan_report_dirs_free(files, nfile);
                return NULL;
            }
            cbm_test_result_t *r = from_junit_dirs(dirs, nd);
            for (int i = 0; i < nd; i++) {
                free(dirs[i]);
            }
            free(dirs);
            cbm_scan_report_dirs_free(files, nfile);
            return r;
        }
        cbm_scan_report_dirs_free(files, nfile);
        return from_stdout_sniff(stdout_data, stdout_len);
    }

    if (strcmp(fmt, "junit_xml") == 0) {
        if (!cwd || !cwd[0]) {
            return NULL;
        }
        char **files = NULL;
        int nfile = 0;
        if (cbm_scan_report_dirs(cwd, &files, &nfile) != 0) {
            return NULL;
        }
        if (nfile == 0) {
            cbm_scan_report_dirs_free(files, nfile);
            cbm_test_result_t *empty = cbm_test_result_new();
            if (!empty) {
                return NULL;
            }
            empty->format = strdup("junit_xml");
            if (!empty->format) {
                cbm_test_result_free(empty);
                return NULL;
            }
            return empty;
        }
        char **dirs = NULL;
        int nd = 0;
        if (collect_unique_dirs_from_files(files, nfile, &dirs, &nd) != 0) {
            cbm_scan_report_dirs_free(files, nfile);
            return NULL;
        }
        cbm_test_result_t *r = from_junit_dirs(dirs, nd);
        for (int i = 0; i < nd; i++) {
            free(dirs[i]);
        }
        free(dirs);
        cbm_scan_report_dirs_free(files, nfile);
        return r;
    }

    if (!stdout_data) {
        return NULL;
    }

    if (strcmp(fmt, "go_test") == 0) {
        return cbm_parse_go_test(stdout_data, stdout_len);
    }
    if (strcmp(fmt, "pytest") == 0) {
        return cbm_parse_pytest(stdout_data, stdout_len);
    }
    if (strcmp(fmt, "sbt") == 0) {
        return cbm_parse_sbt_stdout(stdout_data, stdout_len);
    }

    return NULL;
}
