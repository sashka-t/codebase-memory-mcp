/*
 * run_tests.h — Whitelisted, validated execution of test runner commands (POSIX popen).
 */
#ifndef CBM_RUN_TESTS_H
#define CBM_RUN_TESTS_H

#include <stddef.h>

/* Runs `command` in `cwd` via `cd '<cwd>' && <command>`.
 * Returns 0 on success: `*out_stdout` is malloc'd (possibly empty), `*out_len` is its length,
 * `*out_exit_code` is the shell child exit status (WEXITSTATUS).
 * Returns -1 on failure: `*out_error` is malloc'd when non-NULL; free when done.
 * On failure `*out_stdout` is NULL and `*out_len` is 0 (when pointers non-NULL). */
int cbm_run_tests_exec(const char *command, const char *cwd, char **out_stdout, size_t *out_len,
                       int *out_exit_code, char **out_error);

#endif /* CBM_RUN_TESTS_H */
