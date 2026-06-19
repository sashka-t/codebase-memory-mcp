/*
 * run_tests.c — Whitelist + shell-arg validation + capped popen capture (POSIX).
 */
#include "test_ingest/run_tests.h"

#include "foundation/constants.h"
#include "foundation/compat_fs.h"
#include "foundation/str_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

enum { RUN_TESTS_STDOUT_CAP = CBM_SZ_16 * CBM_SZ_1K * CBM_SZ_1K };

static void clear_outputs(char **out_stdout, size_t *out_len, int *out_exit_code) {
    if (out_stdout) {
        *out_stdout = NULL;
    }
    if (out_len) {
        *out_len = 0;
    }
    if (out_exit_code) {
        *out_exit_code = -1;
    }
}

static int fail(char **out_error, char **out_stdout, size_t *out_len, int *out_exit_code,
                const char *msg) {
    clear_outputs(out_stdout, out_len, out_exit_code);
    if (out_error) {
        *out_error = msg ? strdup(msg) : NULL;
        if (msg && !*out_error) {
            return -1;
        }
    }
    return -1;
}

static const char *skip_ws(const char *s) {
    while (s && (*s == ' ' || *s == '\t')) {
        s++;
    }
    return s;
}

static bool is_only_ws(const char *s) {
    s = skip_ws(s);
    return !s || *s == '\0';
}

static bool is_whitelisted(const char *cmd) {
    const char *c = skip_ws(cmd);
    if (strncmp(c, "gradle ", (size_t)CBM_SZ_7) == 0) {
        return true;
    }
    if (strncmp(c, "./gradlew ", (size_t)CBM_SZ_8 + CBM_SZ_2) == 0) {
        return true;
    }
    if (strncmp(c, "mvn ", (size_t)CBM_SZ_4) == 0) {
        return true;
    }
    if (strncmp(c, "sbt ", (size_t)CBM_SZ_4) == 0) {
        return true;
    }
    if (strncmp(c, "go test", (size_t)CBM_SZ_7) == 0 &&
        (c[CBM_SZ_7] == '\0' || isspace((unsigned char)c[CBM_SZ_7]))) {
        return true;
    }
    if (strncmp(c, "pytest", (size_t)(CBM_SZ_8 - CBM_SZ_2)) == 0 &&
        (c[CBM_SZ_8 - CBM_SZ_2] == '\0' || isspace((unsigned char)c[CBM_SZ_8 - CBM_SZ_2]))) {
        return true;
    }
    return false;
}

/* Reject shell metacharacters but allow quotes and wildcards used in --tests patterns. */
static bool validate_test_command(const char *cmd) {
    for (const char *p = cmd; p && *p; p++) {
        char c = *p;
        if (c == ';' || c == '|' || c == '&' || c == '$' || c == '`' || c == '\n' || c == '\r' ||
            c == '<' || c == '>') {
            return false;
        }
    }
    return true;
}

static int read_stdout_capped(FILE *fp, char **out_stdout, size_t *out_len, char **out_error) {
    char *buf = malloc((size_t)RUN_TESTS_STDOUT_CAP + 1);
    if (!buf) {
        if (out_error) {
            *out_error = strdup("out of memory");
        }
        return -1;
    }
    size_t total = 0;
    for (;;) {
        size_t n = fread(buf + total, 1, (size_t)RUN_TESTS_STDOUT_CAP - total, fp);
        total += n;
        if (n == 0) {
            break;
        }
        if (total >= (size_t)RUN_TESTS_STDOUT_CAP) {
            char tmp;
            if (fread(&tmp, 1, 1, fp) == 1) {
                free(buf);
                if (out_error) {
                    *out_error = strdup("stdout exceeds 16 MiB");
                }
                return -1;
            }
            break;
        }
    }
    buf[total] = '\0';
    *out_stdout = buf;
    *out_len = total;
    return 0;
}

int cbm_run_tests_exec(const char *command, const char *cwd, char **out_stdout, size_t *out_len,
                       int *out_exit_code, char **out_error) {
#if defined(_WIN32)
    (void)command;
    (void)cwd;
    clear_outputs(out_stdout, out_len, out_exit_code);
    return fail(out_error, out_stdout, out_len, out_exit_code,
                "run_tests requires POSIX popen (not supported on Windows)");
#else
    clear_outputs(out_stdout, out_len, out_exit_code);
    if (out_error) {
        *out_error = NULL;
    }

    if (!command || is_only_ws(command)) {
        return fail(out_error, out_stdout, out_len, out_exit_code, "command is empty");
    }
    if (!cwd || !*cwd || is_only_ws(cwd)) {
        return fail(out_error, out_stdout, out_len, out_exit_code, "cwd is empty");
    }
    if (!cbm_validate_shell_arg(cwd)) {
        return fail(out_error, out_stdout, out_len, out_exit_code, "invalid cwd");
    }
    if (!is_whitelisted(command)) {
        return fail(out_error, out_stdout, out_len, out_exit_code,
                    "command is not a whitelisted test runner");
    }
    if (!validate_test_command(command)) {
        return fail(out_error, out_stdout, out_len, out_exit_code,
                    "invalid shell token in test command");
    }

    size_t cwd_len = strlen(cwd);
    size_t cmd_len = strlen(command);
    size_t bufsz = cwd_len + cmd_len + 40;
    char *wrapped = malloc(bufsz);
    if (!wrapped) {
        return fail(out_error, out_stdout, out_len, out_exit_code, "out of memory");
    }
    int nw = snprintf(wrapped, bufsz, "cd '%s' && %s 2>&1", cwd, command);
    if (nw < 0 || (size_t)nw >= bufsz) {
        free(wrapped);
        return fail(out_error, out_stdout, out_len, out_exit_code, "command line too long");
    }

    FILE *fp = cbm_popen(wrapped, "r");
    free(wrapped);
    if (!fp) {
        return fail(out_error, out_stdout, out_len, out_exit_code, "popen failed");
    }

    char *out = NULL;
    size_t olen = 0;
    if (read_stdout_capped(fp, &out, &olen, out_error) != 0) {
        cbm_pclose(fp);
        clear_outputs(out_stdout, out_len, out_exit_code);
        return -1;
    }

    int st = cbm_pclose(fp);
    if (out_stdout) {
        *out_stdout = out;
    } else {
        free(out);
    }
    if (out_len) {
        *out_len = olen;
    }
    if (out_exit_code) {
        if (WIFEXITED(st)) {
            *out_exit_code = WEXITSTATUS(st);
        } else {
            *out_exit_code = -1;
        }
    }
    return 0;
#endif
}
