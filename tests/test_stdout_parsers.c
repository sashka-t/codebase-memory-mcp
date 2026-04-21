/*
 * tests/test_stdout_parsers.c — Unit tests for go test / pytest / sbt stdout parsers.
 */
#include "test_framework.h"
#include "test_ingest/go_test.h"
#include "test_ingest/pytest.h"
#include "test_ingest/sbt.h"
#include "test_ingest/test_result.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(const char *path, size_t *out_len) {
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

TEST(go_test_mixed_fixture) {
    size_t len = 0;
    char *raw = read_all("tests/fixtures/go_test/mixed.txt", &len);
    ASSERT(raw);
    cbm_test_result_t *r = cbm_parse_go_test(raw, len);
    free(raw);
    ASSERT(r);
    ASSERT_STR_EQ(r->format, "go_test");
    ASSERT_EQ((long long)r->suite_count, 1);
    ASSERT_EQ((long long)r->total, 3);
    ASSERT_EQ((long long)r->passed, 1);
    ASSERT_EQ((long long)r->failed, 1);
    ASSERT_EQ((long long)r->skipped, 1);

    cbm_test_suite_t *su = &r->suites[0];
    ASSERT_STR_EQ(su->name, "example.com/demo");
    ASSERT_EQ((long long)su->case_count, 3);
    ASSERT_STR_EQ(su->cases[0].name, "TestPass");
    ASSERT_EQ((long long)su->cases[0].status, (long long)CBM_TEST_STATUS_PASSED);
    ASSERT_STR_EQ(su->cases[1].name, "TestFail");
    ASSERT_EQ((long long)su->cases[1].status, (long long)CBM_TEST_STATUS_FAILED);
    ASSERT(su->cases[1].message && strstr(su->cases[1].message, "want 1 got 2"));
    ASSERT_STR_EQ(su->cases[2].name, "TestSkip");
    ASSERT_EQ((long long)su->cases[2].status, (long long)CBM_TEST_STATUS_SKIPPED);

    cbm_test_result_free(r);
    PASS();
}

TEST(pytest_mixed_fixture) {
    size_t len = 0;
    char *raw = read_all("tests/fixtures/pytest/mixed.txt", &len);
    ASSERT(raw);
    cbm_test_result_t *r = cbm_parse_pytest(raw, len);
    free(raw);
    ASSERT(r);
    ASSERT_STR_EQ(r->format, "pytest");
    ASSERT_EQ((long long)r->suite_count, 1);
    ASSERT_STR_EQ(r->suites[0].name, "tests/unit/test_x.py");
    ASSERT_EQ((long long)r->total, 4);
    ASSERT_EQ((long long)r->passed, 1);
    ASSERT_EQ((long long)r->failed, 1);
    ASSERT_EQ((long long)r->errors, 1);
    ASSERT_EQ((long long)r->skipped, 1);

    cbm_test_suite_t *su = &r->suites[0];
    ASSERT_EQ((long long)su->case_count, 4);
    ASSERT_STR_EQ(su->cases[0].name, "test_ok");
    ASSERT_EQ((long long)su->cases[0].status, (long long)CBM_TEST_STATUS_PASSED);
    ASSERT_STR_EQ(su->cases[1].name, "test_bad");
    ASSERT_EQ((long long)su->cases[1].status, (long long)CBM_TEST_STATUS_FAILED);
    ASSERT_STR_EQ(su->cases[2].name, "test_err");
    ASSERT_EQ((long long)su->cases[2].status, (long long)CBM_TEST_STATUS_ERROR);
    ASSERT_STR_EQ(su->cases[3].name, "test_sk");
    ASSERT_EQ((long long)su->cases[3].status, (long long)CBM_TEST_STATUS_SKIPPED);

    cbm_test_result_free(r);
    PASS();
}

TEST(sbt_mixed_fixture) {
    size_t len = 0;
    char *raw = read_all("tests/fixtures/sbt/mixed.txt", &len);
    ASSERT(raw);
    cbm_test_result_t *r = cbm_parse_sbt_stdout(raw, len);
    free(raw);
    ASSERT(r);
    ASSERT_STR_EQ(r->format, "sbt");
    ASSERT_EQ((long long)r->suite_count, 1);
    ASSERT_STR_EQ(r->suites[0].name, "DemoSuite");
    ASSERT_EQ((long long)r->total, 2);
    ASSERT_EQ((long long)r->passed, 1);
    ASSERT_EQ((long long)r->failed, 1);

    cbm_test_suite_t *su = &r->suites[0];
    ASSERT_EQ((long long)su->case_count, 2);
    /* Order: okCase declared first, then badCase */
    ASSERT_STR_EQ(su->cases[0].name, "okCase");
    ASSERT_EQ((long long)su->cases[0].status, (long long)CBM_TEST_STATUS_PASSED);
    ASSERT_STR_EQ(su->cases[1].name, "badCase");
    ASSERT_EQ((long long)su->cases[1].status, (long long)CBM_TEST_STATUS_FAILED);
    ASSERT_STR_EQ(su->cases[1].message, "expected 1");

    cbm_test_result_free(r);
    PASS();
}

SUITE(stdout_parsers) {
    RUN_TEST(go_test_mixed_fixture);
    RUN_TEST(pytest_mixed_fixture);
    RUN_TEST(sbt_mixed_fixture);
}
