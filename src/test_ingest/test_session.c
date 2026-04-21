/*
 * test_session.c — Test-run session heap object + TTL eviction over CBMHashTable.
 */
#include "test_ingest/test_session.h"

#include "graph_buffer/graph_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_or_generate_run_id(const char *run_id) {
    if (run_id && run_id[0]) {
        return strdup(run_id);
    }
    char buf[CBM_SZ_64];
    time_t t = time(NULL);
    unsigned seq = (unsigned)(rand() & 0xFFFF);
    if (snprintf(buf, sizeof(buf), "run_%ld_%04x", (long)t, seq) >= (int)sizeof(buf)) {
        return NULL;
    }
    return strdup(buf);
}

cbm_test_session_t *cbm_test_session_new(const char *run_id, const char *project, cbm_gbuf_t *graph,
                                         int64_t test_run_node_id, int total, int passed, int failed,
                                         int skipped, int errors, double duration_ms,
                                         const char *command, const char *format, time_t now) {
    if (!graph) {
        return NULL;
    }
    cbm_test_session_t *s = calloc(CBM_ALLOC_ONE, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->run_id = dup_or_generate_run_id(run_id);
    if (!s->run_id) {
        free(s);
        return NULL;
    }
    if (project && project[0]) {
        s->project = strdup(project);
        if (!s->project) {
            free(s->run_id);
            free(s);
            return NULL;
        }
    }
    s->graph = graph;
    s->test_run_node_id = test_run_node_id;
    s->total = total;
    s->passed = passed;
    s->failed = failed;
    s->skipped = skipped;
    s->errors = errors;
    s->duration_ms = duration_ms;
    s->created_at = now;
    s->last_access = now;

    s->command[0] = '\0';
    s->format[0] = '\0';
    if (command) {
        (void)snprintf(s->command, sizeof(s->command), "%s", command);
    }
    if (format) {
        (void)snprintf(s->format, sizeof(s->format), "%s", format);
    }
    return s;
}

void cbm_test_session_free(void *session) {
    cbm_test_session_t *s = session;
    if (!s) {
        return;
    }
    if (s->graph) {
        cbm_gbuf_free(s->graph);
    }
    free(s->run_id);
    free(s->project);
    free(s);
}

typedef struct {
    const char **keys;
    size_t count;
    size_t cap;
    time_t now;
} evict_collect_t;

static void collect_expired_cb(const char *key, void *value, void *userdata) {
    evict_collect_t *c = userdata;
    cbm_test_session_t *s = value;
    if (!s) {
        return;
    }
    if (difftime(c->now, s->last_access) < (double)CBM_TEST_SESSION_TTL_SECONDS) {
        return;
    }
    if (c->count == c->cap) {
        size_t ncap = c->cap ? c->cap * (size_t)CBM_SZ_2 : (size_t)CBM_SZ_8;
        void *p = realloc(c->keys, ncap * sizeof(*c->keys));
        if (!p) {
            return;
        }
        c->keys = p;
        c->cap = ncap;
    }
    c->keys[c->count++] = key;
}

void cbm_test_sessions_evict_expired(CBMHashTable *ht, time_t now) {
    if (!ht) {
        return;
    }
    evict_collect_t c = {.keys = NULL, .count = 0, .cap = 0, .now = now};
    cbm_ht_foreach(ht, collect_expired_cb, &c);
    for (size_t i = 0; i < c.count; i++) {
        void *removed = cbm_ht_delete(ht, c.keys[i]);
        cbm_test_session_free(removed);
    }
    free(c.keys);
}
