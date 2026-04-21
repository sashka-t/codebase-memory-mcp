/*
 * tests/test_run_tests_exec.c — Unit tests for whitelisted test runner execution.
 */
#include "test_framework.h"
#include "test_ingest/run_tests.h"

#include <stdlib.h>
#include <string.h>

TEST(run_tests_rejects_non_whitelist) {
    char *out = NULL;
    size_t len = 0;
    int code = 0;
    char *err = NULL;
    int rc = cbm_run_tests_exec("rm -rf /", "/tmp", &out, &len, &code, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NULL(out);
    ASSERT_NOT_NULL(err);
    ASSERT_NOT_NULL(strstr(err, "whitelisted"));
    free(err);
    PASS();
}

TEST(run_tests_rejects_shell_token) {
    char *out = NULL;
    size_t len = 0;
    int code = 0;
    char *err = NULL;
    int rc = cbm_run_tests_exec("go test; echo pwn", "/tmp", &out, &len, &code, &err);
    ASSERT_EQ(rc, -1);
    ASSERT_NULL(out);
    ASSERT_NOT_NULL(err);
    free(err);
    PASS();
}

TEST(run_tests_go_test_help_capture) {
    char *out = NULL;
    size_t len = 0;
    int code = 0;
    char *err = NULL;
    int rc = cbm_run_tests_exec("go test -help", "/tmp", &out, &len, &code, &err);
    if (rc != 0 || err != NULL) {
        free(out);
        free(err);
        SKIP("go toolchain unavailable");
    }
    ASSERT_NOT_NULL(out);
    ASSERT_TRUE(len > 0);
    ASSERT_NOT_NULL(strstr(out, "usage"));
    (void)code;
    free(out);
    PASS();
}

SUITE(run_tests_exec) {
    RUN_TEST(run_tests_rejects_non_whitelist);
    RUN_TEST(run_tests_rejects_shell_token);
    RUN_TEST(run_tests_go_test_help_capture);
}
