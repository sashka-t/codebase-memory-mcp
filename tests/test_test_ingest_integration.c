/*
 * tests/test_test_ingest_integration.c — MCP test-output tools (ingest, query, list, trace).
 *
 * Exercises ephemeral and persistent paths through cbm_mcp_handle_tool without cloning
 * large repositories. Uses a temp JUnit XML tree + an in-memory store seeded with
 * Function nodes and CALLS edges so trace_test_failures can resolve callers.
 */
#include "../src/foundation/compat.h"
#include "test_framework.h"
#include "test_helpers.h"
#include <mcp/mcp.h>
#include <store/store.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    cbm_clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static char *call_tool_timed(cbm_mcp_server_t *srv, const char *tool, double *ms, const char *args_fmt,
                             ...) {
    char args[4096];
    va_list ap;
    va_start(ap, args_fmt);
    vsnprintf(args, sizeof(args), args_fmt, ap);
    va_end(ap);
    double t0 = now_ms();
    char *resp = cbm_mcp_handle_tool(srv, tool, args);
    if (ms) {
        *ms = now_ms() - t0;
    }
    (void)t0;
    return resp;
}

static bool write_junit_fail(const char *xmlp) {
    FILE *fp = fopen(xmlp, "w");
    if (!fp) {
        return false;
    }
    fputs("<?xml version=\"1.0\"?>\n"
          "<testsuite name=\"demo.suite\" tests=\"1\" failures=\"1\" errors=\"0\" skipped=\"0\" "
          "time=\"0.5\">\n"
          "  <testcase classname=\"demo.fn.Target\" name=\"broke\" time=\"0.1\">\n"
          "    <failure message=\"boom\">boom</failure>\n"
          "  </testcase>\n"
          "</testsuite>\n",
          fp);
    fclose(fp);
    return true;
}

static bool write_junit_ok(const char *xmlp) {
    FILE *fp = fopen(xmlp, "w");
    if (!fp) {
        return false;
    }
    fputs("<?xml version=\"1.0\"?>\n"
          "<testsuite name=\"ok.suite\" tests=\"1\" failures=\"0\" errors=\"0\" skipped=\"0\" "
          "time=\"0.1\">\n"
          "  <testcase classname=\"demo.ok.Cls\" name=\"fine\" time=\"0.1\"/>\n"
          "</testsuite>\n",
          fp);
    fclose(fp);
    return true;
}

TEST(ti_ingest_junit_ephemeral) {
    char *tmp = th_mktempdir("/tmp/cbm-ti-ephem-");
    if (!tmp) {
        PASS();
    }
    char rep[512];
    snprintf(rep, sizeof(rep), "%s/xml", tmp);
    cbm_mkdir(rep);
    char xmlp[512];
    snprintf(xmlp, sizeof(xmlp), "%s/TEST.xml", rep);
    ASSERT_TRUE(write_junit_ok(xmlp));

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);

    char args[2048];
    snprintf(args, sizeof(args),
             "{\"cwd\":\"%s\",\"report_dir\":\"%s\",\"persist\":false}", tmp, rep);

    double ms = 0;
    char *r = call_tool_timed(srv, "ingest_test_reports", &ms, "%s", args);
    ASSERT_NOT_NULL(r);
    ASSERT_NULL(strstr(r, "\"isError\":true"));
    ASSERT_NOT_NULL(strstr(r, "total"));
    ASSERT_NOT_NULL(strstr(r, "passed"));
    ASSERT_NOT_NULL(strstr(r, "ephemeral_"));
    free(r);
    cbm_mcp_server_free(srv);

    remove(xmlp);
    rmdir(rep);
    rmdir(tmp);
    PASS();
}

TEST(ti_ingest_persist_query_list_trace) {
    char pname[64];
    snprintf(pname, sizeof(pname), "ti_integ_%ld", (long)getpid());

    cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
    ASSERT_NOT_NULL(srv);
    cbm_store_t *st = cbm_mcp_server_store(srv);
    ASSERT_NOT_NULL(st);
    ASSERT_EQ(cbm_store_upsert_project(st, pname, "/tmp/ti_integ_root"), CBM_STORE_OK);

    cbm_node_t nc = {0};
    nc.project = pname;
    nc.label = "Function";
    nc.name = "Target";
    nc.qualified_name = "demo.fn.Target";
    nc.file_path = "callee.go";
    nc.start_line = 1;
    nc.end_line = 2;
    nc.properties_json = "{}";
    int64_t id_c = cbm_store_upsert_node(st, &nc);
    ASSERT_TRUE(id_c > 0);

    cbm_node_t nk = {0};
    nk.project = pname;
    nk.label = "Function";
    nk.name = "Caller";
    nk.qualified_name = "demo.pkg.Caller";
    nk.file_path = "caller.go";
    nk.start_line = 3;
    nk.end_line = 4;
    nk.properties_json = "{}";
    int64_t id_k = cbm_store_upsert_node(st, &nk);
    ASSERT_TRUE(id_k > 0);

    cbm_edge_t ce = {0};
    ce.project = pname;
    ce.source_id = id_k;
    ce.target_id = id_c;
    ce.type = "CALLS";
    ce.properties_json = "{}";
    ASSERT_TRUE(cbm_store_insert_edge(st, &ce) > 0);

    char *tmp = th_mktempdir("/tmp/cbm-ti-persist-");
    if (!tmp) {
        cbm_mcp_server_free(srv);
        PASS();
    }
    char rep[512];
    snprintf(rep, sizeof(rep), "%s/xml", tmp);
    cbm_mkdir(rep);
    char xmlp[512];
    snprintf(xmlp, sizeof(xmlp), "%s/TEST.xml", rep);
    ASSERT_TRUE(write_junit_fail(xmlp));

    char args[2048];
    snprintf(args, sizeof(args),
             "{\"cwd\":\"%s\",\"report_dir\":\"%s\",\"persist\":true,\"run_id\":\"ti-run-1\",\"project\":\"%s\"}",
             tmp, rep, pname);

    double ms = 0;
    char *ing = call_tool_timed(srv, "ingest_test_reports", &ms, "%s", args);
    ASSERT_NOT_NULL(ing);
    ASSERT_NULL(strstr(ing, "\"isError\":true"));
    ASSERT_NOT_NULL(strstr(ing, "ti-run-1"));
    ASSERT_NOT_NULL(strstr(ing, "failed"));
    free(ing);

    char *q = call_tool_timed(srv, "query_test_results", &ms,
                              "{\"run_id\":\"ti-run-1\",\"status\":\"failed\",\"limit\":20}");
    ASSERT_NOT_NULL(q);
    ASSERT_NOT_NULL(strstr(q, "broke"));
    ASSERT_NOT_NULL(strstr(q, "failed"));
    free(q);

    char *list = call_tool_timed(srv, "list_test_runs", &ms, "{}");
    ASSERT_NOT_NULL(list);
    ASSERT_NOT_NULL(strstr(list, "ti-run-1"));
    ASSERT_NOT_NULL(strstr(list, "failed"));
    free(list);

    char targs[1024];
    snprintf(targs, sizeof(targs), "{\"run_id\":\"ti-run-1\",\"project\":\"%s\",\"depth\":4}", pname);
    char *tr = call_tool_timed(srv, "trace_test_failures", &ms, "%s", targs);
    ASSERT_NOT_NULL(tr);
    ASSERT_NULL(strstr(tr, "\"isError\":true"));
    ASSERT_NOT_NULL(strstr(tr, "demo.fn.Target"));
    ASSERT_NOT_NULL(strstr(tr, "Caller"));
    ASSERT_NOT_NULL(strstr(tr, "callers"));
    free(tr);

    cbm_mcp_server_free(srv);

    remove(xmlp);
    rmdir(rep);
    rmdir(tmp);
    PASS();
}

SUITE(test_ingest_integration) {
    RUN_TEST(ti_ingest_junit_ephemeral);
    RUN_TEST(ti_ingest_persist_query_list_trace);
}
