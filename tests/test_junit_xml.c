/*
 * tests/test_junit_xml.c — Unit tests for JUnit XML ingestion.
 */
#include "test_framework.h"
#include "test_ingest/junit_xml.h"
#include "test_ingest/test_result.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

TEST(parse_mixed) {
    size_t len = 0;
    char *xml = read_all("tests/fixtures/junit/mixed.xml", &len);
    ASSERT(xml);
    cbm_test_result_t *r = cbm_parse_junit_xml(xml, len);
    free(xml);
    ASSERT(r);
    ASSERT_STR_EQ(r->format, "junit_xml");

    ASSERT_EQ((long long)r->suite_count, 1);
    cbm_test_suite_t *su = &r->suites[0];
    ASSERT_STR_EQ(su->name, "com.example.MyTest");
    ASSERT_EQ((long long)su->case_count, 4);

    ASSERT_EQ((long long)r->total, 4);
    ASSERT_EQ((long long)r->passed, 1);
    ASSERT_EQ((long long)r->failed, 1);
    ASSERT_EQ((long long)r->errors, 1);
    ASSERT_EQ((long long)r->skipped, 1);

    cbm_test_case_t *c0 = &su->cases[0];
    ASSERT_STR_EQ(c0->name, "testPass");
    ASSERT_EQ((long long)c0->status, (long long)CBM_TEST_STATUS_PASSED);
    ASSERT(c0->duration_ms >= 49.0 && c0->duration_ms <= 51.0);

    cbm_test_case_t *c1 = &su->cases[1];
    ASSERT_STR_EQ(c1->name, "testFail");
    ASSERT_EQ((long long)c1->status, (long long)CBM_TEST_STATUS_FAILED);
    ASSERT_STR_EQ(c1->message, "expected 1 but got 2");
    ASSERT_STR_EQ(c1->type, "AssertionError");
    ASSERT(c1->stack_trace && strstr(c1->stack_trace, "stack line 1"));

    cbm_test_case_t *c2 = &su->cases[2];
    ASSERT_STR_EQ(c2->name, "testErr");
    ASSERT_EQ((long long)c2->status, (long long)CBM_TEST_STATUS_ERROR);
    ASSERT_STR_EQ(c2->message, "boom");
    ASSERT(c2->stack_trace && strstr(c2->stack_trace, "err body"));

    cbm_test_case_t *c3 = &su->cases[3];
    ASSERT_STR_EQ(c3->name, "testSkip");
    ASSERT_EQ((long long)c3->status, (long long)CBM_TEST_STATUS_SKIPPED);

    cbm_test_result_free(r);
    PASS();
}

TEST(parse_bare_testsuite_root) {
    size_t len = 0;
    char *xml = read_all("tests/fixtures/junit/bare_testsuite.xml", &len);
    ASSERT(xml);
    cbm_test_result_t *r = cbm_parse_junit_xml(xml, len);
    free(xml);
    ASSERT(r);
    ASSERT_EQ((long long)r->suite_count, 1);
    ASSERT_STR_EQ(r->suites[0].name, "only.suite.Tests");
    ASSERT_EQ((long long)r->suites[0].case_count, 1);
    ASSERT_STR_EQ(r->suites[0].cases[0].name, "okCase");
    cbm_test_result_free(r);
    PASS();
}

TEST(parse_malformed_returns_null) {
    size_t len = 0;
    char *xml = read_all("tests/fixtures/junit/malformed.xml", &len);
    ASSERT(xml);
    cbm_test_result_t *r = cbm_parse_junit_xml(xml, len);
    free(xml);
    ASSERT_NULL(r);
    ASSERT(cbm_junit_xml_last_error() && cbm_junit_xml_last_error()[0]);
    PASS();
}

TEST(parse_dir_merges_xml_files) {
    cbm_test_result_t *r = cbm_parse_junit_xml_dir("tests/fixtures/junit/dir_merge");
    ASSERT(r);
    ASSERT_EQ((long long)r->suite_count, 2);
    ASSERT_EQ((long long)r->total, 2);
    cbm_test_result_free(r);
    PASS();
}

TEST(parse_dir_skips_malformed_file) {
    /* A single corrupt/non-JUnit XML must not abort the whole dir ingest;
     * the good file's suites should still come through. */
    char tmpl[] = "/tmp/cbm-junit-skip-XXXXXX";
    ASSERT(mkdtemp(tmpl));
    char good[256], bad[256];
    snprintf(good, sizeof(good), "%s/TEST-good.xml", tmpl);
    snprintf(bad, sizeof(bad), "%s/TEST-bad.xml", tmpl);
    const char *good_xml =
        "<?xml version=\"1.0\"?>\n<testsuite name=\"ok.Suite\" tests=\"1\">"
        "<testcase classname=\"ok.Suite\" name=\"okCase\"/></testsuite>\n";
    FILE *f = fopen(good, "wb");
    ASSERT(f);
    fputs(good_xml, f);
    fclose(f);
    FILE *b = fopen(bad, "wb");
    ASSERT(b);
    fputs("not xml <<<<", b);
    fclose(b);
    cbm_test_result_t *r = cbm_parse_junit_xml_dir(tmpl);
    ASSERT(r);
    ASSERT_EQ((long long)r->suite_count, 1);
    ASSERT_STR_EQ(r->suites[0].name, "ok.Suite");
    cbm_test_result_free(r);
    remove(good);
    remove(bad);
    remove(tmpl);
    PASS();
}

TEST(parse_dir_all_malformed_returns_null) {
    /* When report files exist but none parse, surface an error (NULL) rather
     * than a silently empty success. */
    char tmpl[] = "/tmp/cbm-junit-allbad-XXXXXX";
    ASSERT(mkdtemp(tmpl));
    char bad[256];
    snprintf(bad, sizeof(bad), "%s/TEST-bad.xml", tmpl);
    FILE *b = fopen(bad, "wb");
    ASSERT(b);
    fputs("not xml <<<<", b);
    fclose(b);
    cbm_test_result_t *r = cbm_parse_junit_xml_dir(tmpl);
    ASSERT_NULL(r);
    remove(bad);
    remove(tmpl);
    PASS();
}

SUITE(junit_xml) {
    RUN_TEST(parse_mixed);
    RUN_TEST(parse_bare_testsuite_root);
    RUN_TEST(parse_malformed_returns_null);
    RUN_TEST(parse_dir_merges_xml_files);
    RUN_TEST(parse_dir_skips_malformed_file);
    RUN_TEST(parse_dir_all_malformed_returns_null);
}
