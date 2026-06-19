/*
 * tests/test_report_scan.c — Unit tests for JUnit XML report directory scanning + format dispatch.
 */
#include "test_framework.h"
#include "test_ingest/report_scan.h"
#include "test_ingest/format_dispatch.h"
#include "test_ingest/test_result.h"
#include "test_helpers.h"

#include <string.h>

static const char kBareXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<testsuite name=\"only.suite.Tests\" tests=\"1\" failures=\"0\" errors=\"0\" skipped=\"0\" time=\"1.0\">\n"
    "  <testcase classname=\"only.suite.Tests\" name=\"okCase\" time=\"0.5\"/>\n"
    "</testsuite>\n";

TEST(scan_finds_gradle_style_xml) {
    char *tmp = th_mktempdir("cbm_rscan");
    ASSERT(tmp);
    ASSERT(th_write_file(TH_PATH(tmp, "build/test-results/test/TEST-Disco.xml"), kBareXml) == 0);
    int n = 0;
    char **paths = NULL;
    ASSERT(cbm_scan_report_dirs(tmp, &paths, &n) == 0);
    ASSERT(n >= 1);
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (strstr(paths[i], "TEST-Disco.xml")) {
            found = 1;
        }
        ASSERT(paths[i][0] == '/');
    }
    ASSERT(found);
    cbm_scan_report_dirs_free(paths, n);
    PASS();
}

TEST(dispatch_auto_prefers_xml) {
    char *tmp = th_mktempdir("cbm_auto");
    ASSERT(tmp);
    ASSERT(th_write_file(TH_PATH(tmp, "build/test-results/test/TEST-Disco.xml"), kBareXml) == 0);
    const char *noise = "tests/unit/test_x.py::t PASSED\n";
    cbm_test_result_t *r = cbm_dispatch_format("auto", tmp, noise, strlen(noise));
    ASSERT(r);
    ASSERT_STR_EQ(r->format, "junit_xml");
    ASSERT_EQ((long long)r->suite_count, 1);
    cbm_test_result_free(r);
    PASS();
}

TEST(dispatch_junit_empty_tree) {
    char *tmp = th_mktempdir("cbm_jmt");
    ASSERT(tmp);
    ASSERT(th_mkdir_p(TH_PATH(tmp, "build/test-results/test")) == 0);
    cbm_test_result_t *r = cbm_dispatch_format("junit_xml", tmp, NULL, 0);
    ASSERT(r);
    ASSERT_STR_EQ(r->format, "junit_xml");
    ASSERT_EQ((long long)r->suite_count, 0);
    cbm_test_result_free(r);
    PASS();
}

TEST(dispatch_auto_stdout_when_no_report_roots) {
    char *tmp = th_mktempdir("cbm_no");
    ASSERT(tmp);
    const char *go = "=== RUN   TestX\n--- PASS: TestX (0.01s)\n";
    cbm_test_result_t *r = cbm_dispatch_format("auto", tmp, go, strlen(go));
    ASSERT(r);
    ASSERT_STR_EQ(r->format, "go_test");
    ASSERT_EQ((long long)r->total, 1);
    cbm_test_result_free(r);
    PASS();
}

TEST(scan_finds_monorepo_submodule_xml) {
    char *tmp = th_mktempdir("cbm_mono");
    ASSERT(tmp);
    ASSERT(th_mkdir_p(TH_PATH(tmp, "employer-service/build/test-results/test")) == 0);
    ASSERT(th_write_file(TH_PATH(tmp, "employer-service/build/test-results/test/TEST-Mono.xml"),
                        kBareXml) == 0);
    int n = 0;
    char **paths = NULL;
    ASSERT(cbm_scan_report_dirs(tmp, &paths, &n) == 0);
    ASSERT(n >= 1);
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (strstr(paths[i], "TEST-Mono.xml")) {
            found = 1;
        }
    }
    ASSERT(found);
    cbm_scan_report_dirs_free(paths, n);
    PASS();
}

SUITE(report_scan) {
    RUN_TEST(scan_finds_gradle_style_xml);
    RUN_TEST(scan_finds_monorepo_submodule_xml);
    RUN_TEST(dispatch_auto_prefers_xml);
    RUN_TEST(dispatch_junit_empty_tree);
    RUN_TEST(dispatch_auto_stdout_when_no_report_roots);
}
