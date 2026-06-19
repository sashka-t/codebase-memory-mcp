/*
 * report_scan.c — Scan Gradle/Maven-style JUnit XML report directories.
 */
#include "test_ingest/report_scan.h"

#include "foundation/compat_fs.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum { REPORT_SCAN_MAX_DEPTH = 6 };

static int append_path(char ***arr, int *count, const char *path) {
    char **nn = (char **)realloc(*arr, (size_t)(*count + 1) * sizeof(*nn));
    if (!nn) {
        return -1;
    }
    *arr = nn;
    (*arr)[*count] = strdup(path);
    if (!(*arr)[*count]) {
        return -1;
    }
    (*count)++;
    return 0;
}

static int ends_with_xml(const char *name) {
    size_t n = strlen(name);
    return n >= 4 && strcmp(name + (n - 4), ".xml") == 0;
}

static int is_dir_path(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static int scan_dir_xml_files(const char *dir, char ***out, int *count) {
    cbm_dir_t *d = cbm_opendir(dir);
    if (!d) {
        return 0;
    }
    for (cbm_dirent_t *e = cbm_readdir(d); e; e = cbm_readdir(d)) {
        if (e->name[0] == '.') {
            continue;
        }
        if (e->is_dir) {
            continue;
        }
        if (!ends_with_xml(e->name)) {
            continue;
        }
        char path[PATH_MAX];
        int nw = snprintf(path, sizeof(path), "%s/%s", dir, e->name);
        if (nw < 0 || (size_t)nw >= sizeof(path)) {
            cbm_closedir(d);
            return -1;
        }
        if (append_path(out, count, path) != 0) {
            cbm_closedir(d);
            return -1;
        }
    }
    cbm_closedir(d);
    return 0;
}

static int scan_build_test_results_children(const char *module_root, char ***out, int *count) {
    char base[PATH_MAX];
    int n = snprintf(base, sizeof(base), "%s/build/test-results", module_root);
    if (n < 0 || (size_t)n >= sizeof(base)) {
        return -1;
    }
    if (!is_dir_path(base)) {
        return 0;
    }
    cbm_dir_t *d = cbm_opendir(base);
    if (!d) {
        return 0;
    }
    for (cbm_dirent_t *e = cbm_readdir(d); e; e = cbm_readdir(d)) {
        if (e->name[0] == '.') {
            continue;
        }
        if (!e->is_dir) {
            continue;
        }
        char sub[PATH_MAX];
        int nw = snprintf(sub, sizeof(sub), "%s/%s", base, e->name);
        if (nw < 0 || (size_t)nw >= sizeof(sub)) {
            cbm_closedir(d);
            return -1;
        }
        if (scan_dir_xml_files(sub, out, count) != 0) {
            cbm_closedir(d);
            return -1;
        }
    }
    cbm_closedir(d);
    return 0;
}

static int should_skip_subdir_name(const char *name) {
    if (!name || !name[0] || name[0] == '.') {
        return 1;
    }
    if (strcmp(name, "node_modules") == 0 || strcmp(name, ".git") == 0 ||
        strcmp(name, "build") == 0 || strcmp(name, "target") == 0 ||
        strcmp(name, "dist") == 0 || strcmp(name, "out") == 0) {
        return 1;
    }
    return 0;
}

static int scan_module_roots(const char *module_root, char ***out, int *count, int *found_roots) {
    char p1[PATH_MAX], p2[PATH_MAX], p3[PATH_MAX], p4[PATH_MAX];
    int n1 = snprintf(p1, sizeof(p1), "%s/build/test-results/test", module_root);
    if (n1 >= 0 && (size_t)n1 < sizeof(p1) && is_dir_path(p1)) {
        (*found_roots)++;
        if (scan_dir_xml_files(p1, out, count) != 0) {
            return -1;
        }
    }
    int n2 = snprintf(p2, sizeof(p2), "%s/build/test-results", module_root);
    if (n2 >= 0 && (size_t)n2 < sizeof(p2) && is_dir_path(p2)) {
        (*found_roots)++;
        if (scan_build_test_results_children(module_root, out, count) != 0) {
            return -1;
        }
    }
    int n3 = snprintf(p3, sizeof(p3), "%s/target/surefire-reports", module_root);
    if (n3 >= 0 && (size_t)n3 < sizeof(p3) && is_dir_path(p3)) {
        (*found_roots)++;
        if (scan_dir_xml_files(p3, out, count) != 0) {
            return -1;
        }
    }
    int n4 = snprintf(p4, sizeof(p4), "%s/target/test-reports", module_root);
    if (n4 >= 0 && (size_t)n4 < sizeof(p4) && is_dir_path(p4)) {
        (*found_roots)++;
        if (scan_dir_xml_files(p4, out, count) != 0) {
            return -1;
        }
    }
    return 0;
}

static int walk_monorepo_modules(const char *dir, int depth, char ***out, int *count, int *found_roots) {
    if (depth > REPORT_SCAN_MAX_DEPTH) {
        return 0;
    }
    if (scan_module_roots(dir, out, count, found_roots) != 0) {
        return -1;
    }
    cbm_dir_t *d = cbm_opendir(dir);
    if (!d) {
        return 0;
    }
    for (cbm_dirent_t *e = cbm_readdir(d); e; e = cbm_readdir(d)) {
        if (!e->is_dir || should_skip_subdir_name(e->name)) {
            continue;
        }
        char sub[PATH_MAX];
        int nw = snprintf(sub, sizeof(sub), "%s/%s", dir, e->name);
        if (nw < 0 || (size_t)nw >= sizeof(sub)) {
            cbm_closedir(d);
            return -1;
        }
        if (walk_monorepo_modules(sub, depth + 1, out, count, found_roots) != 0) {
            cbm_closedir(d);
            return -1;
        }
    }
    cbm_closedir(d);
    return 0;
}

static int cmp_str(const void *a, const void *b) {
    char *const *sa = (char *const *)a;
    char *const *sb = (char *const *)b;
    return strcmp(*sa, *sb);
}

static void sort_unique_paths(char **paths, int *count) {
    int n = *count;
    if (n <= 1) {
        return;
    }
    qsort(paths, (size_t)n, sizeof(paths[0]), cmp_str);
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (w == 0 || strcmp(paths[i], paths[w - 1]) != 0) {
            paths[w++] = paths[i];
        } else {
            free(paths[i]);
        }
    }
    *count = w;
}

int cbm_scan_report_dirs(const char *cwd, char ***out_xml_files, int *out_count) {
    if (!out_xml_files || !out_count) {
        return -1;
    }
    *out_xml_files = NULL;
    *out_count = 0;
    if (!cwd || !cwd[0]) {
        return -1;
    }

    char abs_cwd[PATH_MAX];
    if (!realpath(cwd, abs_cwd)) {
        return -1;
    }

    int found_roots = 0;
    char **paths = NULL;
    int npath = 0;
    if (walk_monorepo_modules(abs_cwd, 0, &paths, &npath, &found_roots) != 0) {
        cbm_scan_report_dirs_free(paths, npath);
        return -1;
    }
    if (found_roots == 0) {
        cbm_scan_report_dirs_free(paths, npath);
        return -1;
    }

    sort_unique_paths(paths, &npath);
    *out_xml_files = paths;
    *out_count = npath;
    return 0;
}

int cbm_probe_report_roots(const char *cwd, int *out_found_roots, int *out_xml_count) {
    if (out_found_roots) {
        *out_found_roots = 0;
    }
    if (out_xml_count) {
        *out_xml_count = 0;
    }
    if (!cwd || !cwd[0]) {
        return -1;
    }
    char abs_cwd[PATH_MAX];
    if (!realpath(cwd, abs_cwd)) {
        return -1;
    }
    int found_roots = 0;
    char **paths = NULL;
    int npath = 0;
    if (walk_monorepo_modules(abs_cwd, 0, &paths, &npath, &found_roots) != 0) {
        cbm_scan_report_dirs_free(paths, npath);
        return -1;
    }
    cbm_scan_report_dirs_free(paths, npath);
    if (out_found_roots) {
        *out_found_roots = found_roots;
    }
    if (out_xml_count) {
        *out_xml_count = npath;
    }
    return found_roots > 0 ? 0 : -1;
}

void cbm_scan_report_dirs_free(char **files, int count) {
    if (!files) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(files[i]);
    }
    free(files);
}
