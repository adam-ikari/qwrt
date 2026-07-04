/*
 * ACE Core C API Unit Tests
 *
 * Tests the C API surface: lifecycle, configuration, tool registration,
 * history, error handling, version, and cancellation.
 *
 * These tests do NOT require a real LLM endpoint — they test the C API
 * behavior without making network calls.
 *
 * Build: cmake --build build --target test_ace_core
 * Run:   ./build/test/test_ace_core
 */

/* _GNU_SOURCE (superset of _POSIX_C_SOURCE) for F_SETPIPE_SZ used in the
 * oversize-line worker test. */
#define _GNU_SOURCE 1

#include <ace.h>
#include <pal_uv.h>
#include <uv.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    fprintf(stderr, "  %s ... ", name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    fprintf(stderr, "PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    tests_failed++; \
    fprintf(stderr, "FAIL: %s\n", msg); \
} while(0)

#define CHECK(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

/* ── Shared test context ─────────────────────────────────────────── */
/*
 * We use a single ace instance for all tests that need one, for efficiency
 * (creating/destroying QuickJS runtimes is expensive). The PAL teardown
 * assertion that previously required fork isolation is fixed.
 */
static qwrt_pal_t *g_pal = NULL;
static ace_t *g_ace = NULL;

/* Write all bytes to a (possibly non-blocking) fd, retrying on EAGAIN with
 * a short yield. Returns 0 on success, -1 on error. */
static int write_all(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0);
                continue;
            }
            return -1;
        }
        written += (size_t)n;
    }
    return 0;
}

/*
 * Destroy an ace + PAL. Previously this required fork() isolation because
 * pal_uv_destroy() hit a libuv uv_close() assertion on tracked timer handles
 * that weren't untracked before free(op). Fixed in pal_uv_http_cleanup()
 * which now untracks all op handles before freeing. Direct destroy is safe.
 */
static void destroy_ace_isolated(ace_t *ace, qwrt_pal_t *pal) {
    if (ace) ace_destroy(ace);
    if (pal) pal_uv_destroy(pal);
}

static void setup_ace(void) {
    if (!g_ace) {
        g_pal = pal_uv_create(uv_default_loop());
        assert(g_pal);
        g_ace = ace_create(g_pal);
        assert(g_ace);
    }
}

/* ── Version tests ───────────────────────────────────────────────── */

static void test_version_string(void) {
    TEST("ace_version returns version string");
    const char *v = ace_version();
    CHECK(v != NULL, "version is NULL");
    CHECK(strcmp(v, ACE_VERSION_STRING) == 0, "version mismatch");
    PASS();
}

static void test_version_components(void) {
    TEST("ace_version_components returns correct values");
    int maj = -1, min = -1, pat = -1;
    ace_version_components(&maj, &min, &pat);
    CHECK(maj == ACE_VERSION_MAJOR, "major mismatch");
    CHECK(min == ACE_VERSION_MINOR, "minor mismatch");
    CHECK(pat == ACE_VERSION_PATCH, "patch mismatch");
    PASS();
}

static void test_version_components_null(void) {
    TEST("ace_version_components handles NULL pointers");
    ace_version_components(NULL, NULL, NULL);  /* should not crash */
    int maj = -1;
    ace_version_components(&maj, NULL, NULL);
    CHECK(maj == ACE_VERSION_MAJOR, "major mismatch with NULL minor/patch");
    PASS();
}

/* ── Lifecycle tests ─────────────────────────────────────────────── */

static void test_create_null_pal(void) {
    TEST("ace_create with NULL PAL returns NULL");
    ace_t *ace = ace_create(NULL);
    CHECK(ace == NULL, "expected NULL");
    PASS();
}

static void test_destroy_null(void) {
    TEST("ace_destroy(NULL) does not crash");
    ace_destroy(NULL);
    PASS();
}

/* ── Configuration tests ─────────────────────────────────────────── */

static void test_set_api_key(void) {
    TEST("ace_set_api_key with valid key");
    setup_ace();
    ace_set_api_key(g_ace, "sk-test-key");
    PASS();
}

static void test_set_api_key_null_ace(void) {
    TEST("ace_set_api_key with NULL ace does not crash");
    ace_set_api_key(NULL, "key");
    PASS();
}

static void test_set_base_url(void) {
    TEST("ace_set_base_url with custom URL");
    setup_ace();
    ace_set_base_url(g_ace, "https://custom.api.com/v1");
    PASS();
}

static void test_set_model(void) {
    TEST("ace_set_model with custom model");
    setup_ace();
    ace_set_model(g_ace, "gpt-4-turbo");
    PASS();
}

/* ── Tool registration tests ─────────────────────────────────────── */

static const char *sync_execute(const char *args_json, void *user) {
    (void)user;
    return strdup("{\"result\":\"ok\"}");
}

static void test_register_tool(void) {
    TEST("ace_register_tool with valid tool");
    setup_ace();

    ace_tool_def_t tool = {
        .name = "test_tool",
        .description = "A test tool",
        .parameters_json = "{\"type\":\"object\",\"properties\":{}}",
        .execute = sync_execute,
        .async = 0,
    };
    int rc = ace_register_tool(g_ace, &tool);
    CHECK(rc == 0, "register failed");
    PASS();
}

static void test_register_tool_duplicate(void) {
    TEST("ace_register_tool duplicate name returns -1");
    setup_ace();

    ace_tool_def_t tool = {
        .name = "dup_tool",
        .description = "Test",
        .parameters_json = "{}",
        .execute = sync_execute,
    };
    CHECK(ace_register_tool(g_ace, &tool) == 0, "first register failed");
    CHECK(ace_register_tool(g_ace, &tool) == -1, "duplicate should return -1");
    PASS();
}

static void test_register_tool_null_ace(void) {
    TEST("ace_register_tool with NULL ace returns -1");
    ace_tool_def_t tool = { .name = "x", .execute = sync_execute };
    CHECK(ace_register_tool(NULL, &tool) == -1, "expected -1");
    PASS();
}

static void test_register_tool_null_name(void) {
    TEST("ace_register_tool with NULL name returns -1");
    setup_ace();

    ace_tool_def_t tool = { .name = NULL, .execute = sync_execute };
    CHECK(ace_register_tool(g_ace, &tool) == -1, "expected -1");
    PASS();
}

static void test_register_async_tool(void) {
    TEST("ace_register_tool with async flag");
    setup_ace();

    ace_tool_def_t tool = {
        .name = "async_test",
        .description = "An async tool",
        .parameters_json = "{}",
        .execute = sync_execute,
        .async = 1,
    };
    CHECK(ace_register_tool(g_ace, &tool) == 0, "register async tool failed");
    PASS();
}

static void test_register_js_tool(void) {
    TEST("ace_register_js_tool with valid params");
    setup_ace();

    int rc = ace_register_js_tool(g_ace,
        "js_test_tool",
        "A JS-implemented test tool",
        "{\"type\":\"object\",\"properties\":{}}",
        "return { result: 'hello from JS' };");
    CHECK(rc == 0, "register_js_tool failed");
    PASS();
}

static void test_register_js_tool_null_ace(void) {
    TEST("ace_register_js_tool with NULL ace returns -1");
    CHECK(ace_register_js_tool(NULL, "x", "d", "{}", "return 1;") == -1, "expected -1 for NULL ace");
    PASS();
}

static void test_register_js_tool_execution(void) {
    TEST("ace_register_js_tool registers and verifies duplicate rejection");
    setup_ace();

    /* Register a JS tool that returns a computed value */
    int rc = ace_register_js_tool(g_ace,
        "add_tool",
        "Adds two numbers",
        "{\"type\":\"object\",\"properties\":{\"a\":{\"type\":\"number\"},\"b\":{\"type\":\"number\"}},\"required\":[\"a\",\"b\"]}",
        "return { result: params.a + params.b };");
    CHECK(rc == 0, "register_js_tool failed");

    /* Verify the tool was registered by checking duplicate rejection */
    rc = ace_register_js_tool(g_ace,
        "add_tool",
        "Duplicate",
        "{}",
        "return null;");
    CHECK(rc == -1, "duplicate js tool should return -1");
    PASS();
}

static void test_register_js_tool_null_name(void) {
    TEST("ace_register_js_tool with NULL name returns -1");
    setup_ace();
    CHECK(ace_register_js_tool(g_ace, NULL, "d", "{}", "return 1;") == -1, "expected -1 for NULL name");
    PASS();
}

/* ── Run lifecycle tests ─────────────────────────────────────────── */

static void test_run_null_ace(void) {
    TEST("ace_run with NULL ace returns -1");
    ace_callbacks_t cbs = {0};
    CHECK(ace_run(NULL, "test", &cbs, NULL) == -1, "expected -1");
    PASS();
}

static void test_run_null_input(void) {
    TEST("ace_run with NULL input returns -1");
    setup_ace();
    ace_callbacks_t cbs = {0};
    CHECK(ace_run(g_ace, NULL, &cbs, NULL) == -1, "expected -1");
    PASS();
}

static void test_run_null_cbs(void) {
    TEST("ace_run with NULL callbacks returns -1");
    setup_ace();
    CHECK(ace_run(g_ace, "test", NULL, NULL) == -1, "expected -1");
    PASS();
}

/* ── Poll tests ──────────────────────────────────────────────────── */

static void test_poll_null_ace(void) {
    TEST("ace_poll with NULL ace returns 0");
    CHECK(ace_poll(NULL, 0) == 0, "expected 0");
    CHECK(ace_poll(NULL, -1) == 0, "expected 0");
    PASS();
}

static void test_poll_idle(void) {
    TEST("ace_poll when idle returns 0");
    setup_ace();
    int rc = ace_poll(g_ace, 0);
    CHECK(rc == 0, "expected 0 when idle");
    PASS();
}

static void test_poll_error_code(void) {
    TEST("ace_poll returns -1 when run completes with error");
    setup_ace();

    /* Configure with unreachable endpoint to trigger network error.
     * Using 127.0.0.1:1 (localhost, no listener) for fast failure. */
    ace_set_api_key(g_ace, "invalid-key");
    ace_set_base_url(g_ace, "http://127.0.0.1:1/v1");  /* no listener, fast ECONNREFUSED */
    ace_set_model(g_ace, "gpt-4");

    ace_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));

    /* Start a run — should fail with network error */
    int rc = ace_run(g_ace, "Hello", &cbs, NULL);
    CHECK(rc == 0, "ace_run should start");

    /* Block until done — connection refused should happen quickly */
    rc = ace_poll(g_ace, -1);
    /* rc should be -1 (error) or 0 (success) — in any case, not 1 (still running) */
    CHECK(rc != 1, "poll should not return 1 (still running)");

    /* Verify last_error is set if poll returned -1 */
    if (rc == -1) {
        const char *err = ace_last_error(g_ace);
        CHECK(err != NULL, "ace_last_error should be set when poll returns -1");
    }

    PASS();
}

/* ── History tests ───────────────────────────────────────────────── */

static void test_clear_history_null(void) {
    TEST("ace_clear_history with NULL ace does not crash");
    ace_clear_history(NULL);
    PASS();
}

static void test_get_history_json_null(void) {
    TEST("ace_get_history_json with NULL ace returns NULL");
    char *h = ace_get_history_json(NULL);
    CHECK(h == NULL, "expected NULL");
    PASS();
}

static void test_clear_history(void) {
    TEST("ace_clear_history on idle ace");
    setup_ace();
    ace_clear_history(g_ace);
    PASS();
}

static void test_get_history_json_empty(void) {
    TEST("ace_get_history_json returns empty array for new ace");
    setup_ace();
    char *h = ace_get_history_json(g_ace);
    CHECK(h != NULL, "expected non-NULL");
    ace_free(h);
    PASS();
}

/* ── Error tests ─────────────────────────────────────────────────── */

static void test_last_error_null(void) {
    TEST("ace_last_error with NULL ace returns NULL");
    CHECK(ace_last_error(NULL) == NULL, "expected NULL");
    PASS();
}

static void test_last_error_initial(void) {
    TEST("ace_last_error returns NULL on fresh ace");
    setup_ace();
    CHECK(ace_last_error(g_ace) == NULL, "expected NULL");
    PASS();
}

static void test_last_error_code(void) {
    TEST("ace_last_error_code returns correct codes");
    CHECK(ace_last_error_code(NULL) == ACE_ERR_INVALID_ARG, "expected ACE_ERR_INVALID_ARG for NULL ace");

    setup_ace();
    /* Fresh ace should have no error */
    ace_error_t code = ace_last_error_code(g_ace);
    CHECK(code == ACE_OK, "expected ACE_OK on fresh ace");
    PASS();
}

/* ── Reset tests ──────────────────────────────────────────────────── */

static void test_reset_clears_state(void) {
    TEST("ace_reset clears tools, config, and history");
    setup_ace();

    /* Configure the ace */
    ace_set_api_key(g_ace, "sk-test");
    ace_set_base_url(g_ace, "https://api.example.com/v1");
    ace_set_model(g_ace, "gpt-4");

    /* Register a tool */
    ace_tool_def_t tool = {
        .name = "pre_reset_tool",
        .description = "Test",
        .parameters_json = "{}",
        .execute = sync_execute,
    };
    CHECK(ace_register_tool(g_ace, &tool) == 0, "register before reset failed");

    /* Reset the ace */
    ace_reset(g_ace);

    /* After reset, the same tool name should be registrable again
     * (no duplicate rejection since tools were cleared) */
    CHECK(ace_register_tool(g_ace, &tool) == 0, "register after reset failed");
    PASS();
}

static void test_reset_null_ace(void) {
    TEST("ace_reset with NULL ace does not crash");
    ace_reset(NULL);
    PASS();
}

/* ── Cancellation tests ──────────────────────────────────────────── */

static void test_cancel_null(void) {
    TEST("ace_cancel with NULL ace does not crash");
    ace_cancel(NULL);
    PASS();
}

static void test_cancel_idle(void) {
    TEST("ace_cancel when idle is safe");
    setup_ace();
    ace_cancel(g_ace);
    PASS();
}

static void test_cancel_during_poll(void) {
    TEST("ace_cancel terminates poll loop");
    setup_ace();

    /* Cancel before poll — poll should return immediately since no run */
    ace_cancel(g_ace);
    CHECK(ace_poll(g_ace, -1) == 0, "poll should return 0 when idle");
    PASS();
}

/* Regression: on_done must fire exactly once per run, even when the
 * JS-initiated completion and a C completion path (here: cancel) could
 * both fire. Uses an unreachable endpoint so the run does not hang. */
static int g_done_fire_count;
static void counting_on_done(const char *error, void *user) {
    (void)error; (void)user;
    g_done_fire_count++;
}

static void test_on_done_fires_once(void) {
    TEST("on_done fires exactly once per run");
    setup_ace();

    /* Unreachable endpoint → run completes quickly via error path.
     * 127.0.0.1:1 has no listener, so ECONNREFUSED fires fast. */
    ace_set_api_key(g_ace, "sk-test");
    ace_set_base_url(g_ace, "http://127.0.0.1:1/v1");
    ace_set_model(g_ace, "gpt-4");

    ace_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.on_done = counting_on_done;
    g_done_fire_count = 0;

    int rc = ace_run(g_ace, "Hello", &cbs, NULL);
    CHECK(rc == 0, "ace_run should start");

    /* Drive the run to completion. Cancel mid-way to exercise the
     * C cancel path racing the JS error-completion path. */
    ace_cancel(g_ace);
    /* Poll until the run is no longer active (a few short polls). */
    for (int i = 0; i < 50; i++) {
        int p = ace_poll(g_ace, 10);
        if (p != 1) break;  /* 0 or -1 means done */
    }
    /* Drain any remaining ticks without re-arming a run */
    ace_poll(g_ace, 0);
    ace_poll(g_ace, 0);

    CHECK(g_done_fire_count == 1, "on_done should fire exactly once");
    PASS();
}

/*
 * Regression: ace_cancel must abort an in-flight HTTP stream. We connect to
 * a local TCP sink that accepts but never responds (so the read would hang
 * until the PAL's 60s idle timeout). ace_cancel is called from another
 * thread shortly after the run starts; the run must complete well within
 * the timeout (proving the abort woke the loop / tore down the stream),
 * with on_done firing exactly once.
 */

/* A tiny TCP sink: accept one connection and hold it open without responding,
 * so an HTTP stream read blocks. Runs in its own thread. */
static void *sink_thread(void *arg) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return NULL;
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    /* Bound accept timeout so a cancelled connection (never established)
     * doesn't block this thread forever. */
    struct timeval ato = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &ato, sizeof(ato));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  /* ephemeral */
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(srv); return NULL; }
    if (listen(srv, 1) < 0) { close(srv); return NULL; }

    socklen_t alen = sizeof(addr);
    getsockname(srv, (struct sockaddr *)&addr, &alen);
    *(int *)arg = ntohs(addr.sin_port);  /* publish the chosen port */
    /* Wake the parent that is waiting for the port. */
    int conn = accept(srv, NULL, NULL);
    if (conn < 0) {
        /* accept timed out (connection was aborted before establishing) —
         * expected when ace_cancel fires during connect. Just exit. */
        close(srv);
        return NULL;
    }
    /* Hold the connection open; the test cancels (within ~200ms-300ms),
     * which aborts the PAL stream and closes this socket (recv returns
     * 0/EOF). RCVTIMEO is set well ABOVE the test's 10s budget (30s) so that
     * ONLY a real abort unblocks the client — if the cancel/abort path is
     * broken, the test hits its own 10s deadline and fails, rather than
     * passing thanks to this sink timing out. */
    {
        struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
        setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char buf[64];
        /* drain whatever the client sends, then sleep */
        while (recv(conn, buf, sizeof(buf), 0) > 0) {}
        close(conn);
    }
    close(srv);
    return NULL;
}

/*
 * Regression: a stale `cancelled` flag left by a post-run ace_cancel must not
 * immediately cancel the NEXT run. ace_run now clears cancelled, so a fresh
 * run proceeds (running stays 1) instead of firing on_done("Cancelled") on
 * its first poll. We use a sink that never responds, so the run stays alive
 * (running==1) — if the stale flag weren't cleared, ace_poll would return 0
 * (cancelled) on the first iteration.
 */
static void test_stale_cancel_does_not_cancel_next_run(void) {
    TEST("stale cancelled flag does not cancel the next run");
    setup_ace();

    /* Set a stale cancel flag as if a prior run was cancelled. */
    ace_cancel(g_ace);

    int port = 0;
    pthread_t sink_tid;
    CHECK(pthread_create(&sink_tid, NULL, sink_thread, &port) == 0,
          "failed to create sink thread");
    for (int i = 0; i < 100 && port == 0; i++) {
        struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL);
    }
    CHECK(port != 0, "sink server did not start");

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/v1", port);
    ace_set_api_key(g_ace, "sk-test");
    ace_set_base_url(g_ace, url);
    ace_set_model(g_ace, "gpt-4");

    ace_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));

    CHECK(ace_run(g_ace, "Hello", &cbs, NULL) == 0, "ace_run should start");

    /* Poll briefly. If the stale flag cancelled the run, ace_poll returns 0
     * (done) almost immediately. With the fix, the run stays running (returns
     * 1) for at least a short window because the sink never responds. */
    int still_running = 0;
    for (int i = 0; i < 30; i++) {
        int rc = ace_poll(g_ace, 10);
        if (rc == 1) { still_running = 1; break; }
        if (rc != 1) break;  /* done/cancelled */
    }
    CHECK(still_running, "run should still be running (stale flag did not cancel it)");

    /* Now genuinely cancel to clean up the run and reap the sink. */
    ace_cancel(g_ace);
    for (int i = 0; i < 100; i++) {
        if (ace_poll(g_ace, 10) != 1) break;
    }
    pthread_join(sink_tid, NULL);
    PASS();
}

static int g_abort_done_count;
static void counting_on_done_abort(const char *error, void *user) {
    (void)error; (void)user;
    g_abort_done_count++;
}

static void test_cancel_aborts_http_stream(void) {
    TEST("ace_cancel aborts an in-flight HTTP stream");
    setup_ace();

    /* Start the sink server on an ephemeral port. */
    int port = 0;
    pthread_t tid;
    int rc = pthread_create(&tid, NULL, sink_thread, &port);
    CHECK(rc == 0, "failed to create sink thread");

    /* Wait for the sink to publish a port (poll briefly). */
    for (int i = 0; i < 100 && port == 0; i++) {
        struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL);
    }
    CHECK(port != 0, "sink server did not start");

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/v1", port);
    ace_set_api_key(g_ace, "sk-test");
    ace_set_base_url(g_ace, url);
    ace_set_model(g_ace, "gpt-4");

    ace_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.on_done = counting_on_done_abort;
    g_abort_done_count = 0;

    rc = ace_run(g_ace, "Hello", &cbs, NULL);
    CHECK(rc == 0, "ace_run should start");

    /* Give the connect a moment to establish, then cancel from this thread.
     * (Same thread as the poll — the test is single-threaded for the poll,
     * but ace_cancel is documented as safe from any thread, including the
     * polling thread.) */
    struct timespec ts = {0, 200 * 1000000}; nanosleep(&ts, NULL);  /* 200ms */
    ace_cancel(g_ace);

    /* Poll to completion. Must finish well under the 60s idle timeout. */
    int64_t deadline = (int64_t)time(NULL) + 10;  /* 10s budget */
    int poll_rc = 1;
    while (poll_rc == 1 && (int64_t)time(NULL) < deadline) {
        poll_rc = ace_poll(g_ace, 100);
    }

    CHECK(poll_rc != 1, "run should complete within 10s of cancel (abort worked)");
    CHECK(g_abort_done_count == 1, "on_done should fire exactly once on cancel");

    /* Reap the sink thread. */
    pthread_join(tid, NULL);
    PASS();
}

/*
 * Cross-thread cancel: ace_cancel is called from a SEPARATE thread while the
 * owner thread is driving the run with short, non-blocking polls. The
 * cancelled flag (atomic) is observed on the next poll iteration, which tears
 * down the in-flight HTTP stream via pal->http_abort and fires on_done.
 *
 * (This covers the realistic concurrent case — a UI/signal thread cancelling
 * while the owner pumps the loop. It does NOT assert that a uv_run blocked
 * mid-read is woken from another thread; that cross-thread wake-up is a
 * documented latency limitation of ace_cancel, see the note in ace_cancel.)
 */
static ace_t *g_cancel_target;
static void *cancel_after_delay_thread(void *arg) {
    int delay_ms = *(int *)arg;
    struct timespec ts = { .tv_sec = delay_ms / 1000,
                           .tv_nsec = (delay_ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
    ace_cancel(g_cancel_target);  /* from another thread */
    return NULL;
}

static void test_cancel_from_another_thread(void) {
    TEST("ace_cancel from another thread aborts the run");
    setup_ace();

    int port = 0;
    pthread_t sink_tid;
    CHECK(pthread_create(&sink_tid, NULL, sink_thread, &port) == 0,
          "failed to create sink thread");
    for (int i = 0; i < 100 && port == 0; i++) {
        struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL);
    }
    CHECK(port != 0, "sink server did not start");

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/v1", port);
    ace_set_api_key(g_ace, "sk-test");
    ace_set_base_url(g_ace, url);
    ace_set_model(g_ace, "gpt-4");

    ace_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.on_done = counting_on_done_abort;
    g_abort_done_count = 0;

    CHECK(ace_run(g_ace, "Hello", &cbs, NULL) == 0, "ace_run should start");

    /* Arm a cancel from another thread after a short delay, while the owner
     * drives the loop with brief non-blocking polls. */
    g_cancel_target = g_ace;
    int delay = 200;  /* ms */
    pthread_t cancel_tid;
    CHECK(pthread_create(&cancel_tid, NULL, cancel_after_delay_thread, &delay) == 0,
          "failed to create cancel thread");

    int64_t deadline = (int64_t)time(NULL) + 10;  /* 10s budget (<< 60s timeout) */
    int poll_rc = 1;
    while (poll_rc == 1 && (int64_t)time(NULL) < deadline) {
        poll_rc = ace_poll(g_ace, 0);  /* non-blocking; observes cancelled each pass */
        if (poll_rc == 1) {
            struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL);  /* 1ms */
        }
    }

    CHECK(poll_rc != 1, "run should complete within 10s of cross-thread cancel");
    CHECK(g_abort_done_count == 1, "on_done should fire exactly once");

    pthread_join(cancel_tid, NULL);
    pthread_join(sink_tid, NULL);
    PASS();
}

/* ── Async tool result tests ─────────────────────────────────────── */

static void test_provide_tool_result_null(void) {
    TEST("ace_provide_tool_result with NULL ace returns -1");
    CHECK(ace_provide_tool_result(NULL, "tool", "{}") == -1, "expected -1");
    PASS();
}

static void test_provide_tool_result_no_pending(void) {
    TEST("ace_provide_tool_result with no pending tool returns -1");
    setup_ace();
    CHECK(ace_provide_tool_result(g_ace, "nonexistent", "{}") == -1, "expected -1");
    PASS();
}

/* ── Worker mode tests ───────────────────────────────────────────── */

static void test_worker_tick_null(void) {
    TEST("ace_worker_tick with NULL ace returns 0");
    CHECK(ace_worker_tick(NULL) == 0, "expected 0 for NULL ace");
    PASS();
}

static void test_worker_tick_non_worker(void) {
    TEST("ace_worker_tick returns 0 when not in worker mode");
    setup_ace();
    /* Non-worker ace: _ace_worker_running is false, so tick returns 0 */
    CHECK(ace_worker_tick(g_ace) == 0, "expected 0 for non-worker ace");
    PASS();
}

/*
 * Test: ace_worker_tick with stdin containing valid JSON-RPC.
 *
 * We create a pipe, dup2 it onto stdin, then create a worker-mode ace
 * and write a "configure" JSON-RPC message.  ace_worker_tick processes
 * stdin and the bridge writes the response to stdout (which we also
 * redirect to a pipe).
 *
 * This verifies the IPC dispatch path: stdin → readline → parse →
 * dispatch → configure → stdout writeline.
 */
static void test_worker_ipc_configure(void) {
    TEST("ace_worker_tick processes JSON-RPC configure via stdin pipe");

    /* Save original fds */
    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    CHECK(saved_stdin >= 0 && saved_stdout >= 0, "failed to save fds");

    /* Create pipes for stdin and stdout */
    int stdin_pipe[2], stdout_pipe[2];
    CHECK(pipe(stdin_pipe) == 0, "stdin pipe failed");
    CHECK(pipe(stdout_pipe) == 0, "stdout pipe failed");

    /* Set both pipes to non-blocking */
    int flags;
    flags = fcntl(stdin_pipe[1], F_GETFL, 0);
    fcntl(stdin_pipe[1], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    /* Redirect stdin to read from pipe, stdout to write to pipe */
    CHECK(dup2(stdin_pipe[0], STDIN_FILENO) >= 0, "dup2 stdin failed");
    CHECK(dup2(stdout_pipe[1], STDOUT_FILENO) >= 0, "dup2 stdout failed");

    /* Set worker mode BEFORE ace_create */
    setenv("ACE_WORKER", "1", 1);

    /* Create ace — extension init will set _ace_is_worker=true and
     * _ace_worker_init() will set _ace_worker_running=true */
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    CHECK(pal != NULL, "pal_uv_create failed");

    ace_t *ace = ace_create(pal);
    CHECK(ace != NULL, "ace_create with ACE_WORKER=1 failed");

    /* Close the read end of stdin pipe and write end of stdout pipe
     * (we write to stdin_pipe[1], read from stdout_pipe[0]) */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    /* Drain any initial output (the bridge may write during init) */
    {
        char drain[4096];
        ssize_t n;
        while ((n = read(stdout_pipe[0], drain, sizeof(drain) - 1)) > 0) {
            /* discard initial output */
        }
    }

    /* Write a JSON-RPC configure request to the worker's stdin */
    const char *jsonrpc = "{\"jsonrpc\":\"2.0\",\"method\":\"configure\","
        "\"params\":{\"api_key\":\"sk-test\",\"model\":\"gpt-4\"},\"id\":1}\n";
    size_t msg_len = strlen(jsonrpc);
    size_t written = 0;
    while (written < msg_len) {
        ssize_t n = write(stdin_pipe[1], jsonrpc + written, msg_len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Poll a bit for pipe to become writable */
                do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0);
                continue;
            }
            break;
        }
        written += (size_t)n;
    }

    /* Close the write end to signal EOF */
    close(stdin_pipe[1]);

    /* Process the message via ace_worker_tick */
    int keep = ace_worker_tick(ace);
    /* Should keep running — we only sent configure, not shutdown */
    CHECK(keep == 1, "worker should keep running after configure");

    /* Drive the event loop so any async work completes */
    ace_poll(ace, 0);

    /* Read the response from stdout */
    char response[4096];
    memset(response, 0, sizeof(response));
    ssize_t total = 0;
    int retries = 100;
    while (retries-- > 0 && total < (ssize_t)sizeof(response) - 1) {
        ssize_t n = read(stdout_pipe[0], response + total,
                         sizeof(response) - 1 - (size_t)total);
        if (n > 0) {
            total += n;
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0);
                continue;
            }
            break;
        } else {
            /* n == 0 means EOF */
            break;
        }
    }
    response[total] = '\0';

    /* Verify response is valid JSON-RPC with result */
    CHECK(strlen(response) > 0, "expected JSON-RPC response");
    CHECK(strstr(response, "\"jsonrpc\"") != NULL ||
          strstr(response, "\"id\"") != NULL,
          "response should be JSON-RPC");

    /* Cleanup */
    close(stdout_pipe[0]);
    destroy_ace_isolated(ace, pal);

    /* Restore original fds */
    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);

    /* Clear ACE_WORKER for subsequent tests */
    unsetenv("ACE_WORKER");

    PASS();
}

/*
 * Test: ace_worker_tick processes shutdown request.
 *
 * Sends a shutdown JSON-RPC message and verifies that the worker
 * signals it should stop (returns 0 after the run loop drains).
 */
static void test_worker_ipc_shutdown(void) {
    TEST("ace_worker_tick processes shutdown and returns 0");

    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    CHECK(saved_stdin >= 0 && saved_stdout >= 0, "failed to save fds");

    int stdin_pipe[2], stdout_pipe[2];
    CHECK(pipe(stdin_pipe) == 0, "stdin pipe failed");
    CHECK(pipe(stdout_pipe) == 0, "stdout pipe failed");

    int flags;
    flags = fcntl(stdin_pipe[1], F_GETFL, 0);
    fcntl(stdin_pipe[1], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    CHECK(dup2(stdin_pipe[0], STDIN_FILENO) >= 0, "dup2 stdin failed");
    CHECK(dup2(stdout_pipe[1], STDOUT_FILENO) >= 0, "dup2 stdout failed");

    setenv("ACE_WORKER", "1", 1);

    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    CHECK(pal != NULL, "pal_uv_create failed");

    ace_t *ace = ace_create(pal);
    CHECK(ace != NULL, "ace_create failed");

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    /* Write shutdown request */
    const char *jsonrpc = "{\"jsonrpc\":\"2.0\",\"method\":\"shutdown\",\"id\":1}\n";
    size_t msg_len = strlen(jsonrpc);
    size_t written = 0;
    while (written < msg_len) {
        ssize_t n = write(stdin_pipe[1], jsonrpc + written, msg_len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0); continue; }
            break;
        }
        written += (size_t)n;
    }
    close(stdin_pipe[1]);

    /* First tick should process shutdown request */
    ace_worker_tick(ace);
    ace_poll(ace, 0);

    /* Second tick should detect shutdown is done and return 0 */
    int keep = ace_worker_tick(ace);
    CHECK(keep == 0, "worker should stop after shutdown");

    /* Read and verify the response */
    char response[4096];
    memset(response, 0, sizeof(response));
    ssize_t total = 0;
    int retries = 100;
    while (retries-- > 0 && total < (ssize_t)sizeof(response) - 1) {
        ssize_t n = read(stdout_pipe[0], response + total,
                         sizeof(response) - 1 - (size_t)total);
        if (n > 0) total += n;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0); continue; }
        else break;
    }
    response[total] = '\0';

    CHECK(strlen(response) > 0, "expected JSON-RPC shutdown response");

    close(stdout_pipe[0]);
    destroy_ace_isolated(ace, pal);

    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    unsetenv("ACE_WORKER");

    PASS();
}

/*
 * Test: JSON-RPC error handling for unknown methods.
 *
 * Sends a request with an unknown method name and verifies that
 * the bridge returns a proper JSON-RPC error response.
 */
static void test_worker_ipc_unknown_method(void) {
    TEST("ace_worker_tick returns JSON-RPC error for unknown method");

    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    CHECK(saved_stdin >= 0 && saved_stdout >= 0, "failed to save fds");

    int stdin_pipe[2], stdout_pipe[2];
    CHECK(pipe(stdin_pipe) == 0, "stdin pipe failed");
    CHECK(pipe(stdout_pipe) == 0, "stdout pipe failed");

    int flags;
    flags = fcntl(stdin_pipe[1], F_GETFL, 0);
    fcntl(stdin_pipe[1], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    CHECK(dup2(stdin_pipe[0], STDIN_FILENO) >= 0, "dup2 stdin failed");
    CHECK(dup2(stdout_pipe[1], STDOUT_FILENO) >= 0, "dup2 stdout failed");

    setenv("ACE_WORKER", "1", 1);

    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    CHECK(pal != NULL, "pal_uv_create failed");

    ace_t *ace = ace_create(pal);
    CHECK(ace != NULL, "ace_create failed");

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    /* Write a request with unknown method */
    const char *jsonrpc = "{\"jsonrpc\":\"2.0\",\"method\":\"nonexistent_method\",\"id\":99}\n";
    size_t msg_len = strlen(jsonrpc);
    size_t written = 0;
    while (written < msg_len) {
        ssize_t n = write(stdin_pipe[1], jsonrpc + written, msg_len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0); continue; }
            break;
        }
        written += (size_t)n;
    }
    close(stdin_pipe[1]);

    ace_worker_tick(ace);
    ace_poll(ace, 0);

    char response[4096];
    memset(response, 0, sizeof(response));
    ssize_t total = 0;
    int retries = 100;
    while (retries-- > 0 && total < (ssize_t)sizeof(response) - 1) {
        ssize_t n = read(stdout_pipe[0], response + total,
                         sizeof(response) - 1 - (size_t)total);
        if (n > 0) total += n;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0); continue; }
        else break;
    }
    response[total] = '\0';

    /* Should get an error response with code -32601 (Method not found) */
    CHECK(strlen(response) > 0, "expected JSON-RPC error response");
    CHECK(strstr(response, "-32601") != NULL ||
          strstr(response, "Method not found") != NULL,
          "expected method-not-found error");

    close(stdout_pipe[0]);
    destroy_ace_isolated(ace, pal);

    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    unsetenv("ACE_WORKER");

    PASS();
}

/*
 * Regression: a tool_result for an unknown (no matching pending) tool must
 * be rejected with a JSON-RPC error, not silently swallowed. Otherwise the
 * host believes the result was applied while the run's Promise stays pending
 * and the conversation hangs.
 */
static void test_worker_ipc_unknown_tool_result(void) {
    TEST("worker rejects unknown tool_result with error");

    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    CHECK(saved_stdin >= 0 && saved_stdout >= 0, "failed to save fds");

    int stdin_pipe[2], stdout_pipe[2];
    CHECK(pipe(stdin_pipe) == 0, "stdin pipe failed");
    CHECK(pipe(stdout_pipe) == 0, "stdout pipe failed");

    int flags;
    flags = fcntl(stdin_pipe[1], F_GETFL, 0);
    fcntl(stdin_pipe[1], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    CHECK(dup2(stdin_pipe[0], STDIN_FILENO) >= 0, "dup2 stdin failed");
    CHECK(dup2(stdout_pipe[1], STDOUT_FILENO) >= 0, "dup2 stdout failed");

    setenv("ACE_WORKER", "1", 1);
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    CHECK(pal != NULL, "pal_uv_create failed");
    ace_t *ace = ace_create(pal);
    CHECK(ace != NULL, "ace_create failed");

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    /* Drain init output */
    {
        char drain[4096];
        ssize_t n;
        while ((n = read(stdout_pipe[0], drain, sizeof(drain) - 1)) > 0) {}
    }

    /* Send a tool_result for a tool that was never registered/started. */
    const char *jsonrpc = "{\"jsonrpc\":\"2.0\",\"method\":\"tool_result\","
        "\"params\":{\"name\":\"no_such_tool\",\"result_json\":\"{}\"},\"id\":7}\n";
    CHECK(write_all(stdin_pipe[1], jsonrpc, strlen(jsonrpc)) == 0, "write failed");
    /* Close stdin write end so the worker's readline sees EOF and returns
     * instead of blocking for more input (the worker reads synchronously). */
    close(stdin_pipe[1]);

    ace_worker_tick(ace);
    ace_poll(ace, 0);

    char response[4096];
    memset(response, 0, sizeof(response));
    ssize_t total = 0;
    int retries = 100;
    while (retries-- > 0 && total < (ssize_t)sizeof(response) - 1) {
        ssize_t n = read(stdout_pipe[0], response + total,
                         sizeof(response) - 1 - (size_t)total);
        if (n > 0) total += n;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0);
            continue;
        }
        else break;
    }
    response[total] = '\0';

    CHECK(strstr(response, "-32602") != NULL ||
          strstr(response, "unknown pending tool") != NULL,
          "expected unknown-tool error");

    /* stdin_pipe[1] already closed above (before tick) to signal EOF. */
    close(stdout_pipe[0]);
    destroy_ace_isolated(ace, pal);

    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    unsetenv("ACE_WORKER");

    PASS();
}

/* ── ace_free tests ──────────────────────────────────────────────── */

static void test_ace_free_null(void) {
    TEST("ace_free(NULL) does not crash");
    ace_free(NULL);
    PASS();
}

/*
 * Regression: a JSON-RPC line larger than the 64 KiB stdin buffer must
 * be rejected with a JSON-RPC error rather than silently dropped (which
 * would hang the host waiting for an ack).
 */
static void test_worker_ipc_oversize_line(void) {
    TEST("worker rejects oversize JSON-RPC line with error");

    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    CHECK(saved_stdin >= 0 && saved_stdout >= 0, "failed to save fds");

    int stdin_pipe[2], stdout_pipe[2];
    CHECK(pipe(stdin_pipe) == 0, "stdin pipe failed");
    CHECK(pipe(stdout_pipe) == 0, "stdout pipe failed");

    int flags;
    flags = fcntl(stdin_pipe[1], F_GETFL, 0);
    fcntl(stdin_pipe[1], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    CHECK(dup2(stdin_pipe[0], STDIN_FILENO) >= 0, "dup2 stdin failed");
    CHECK(dup2(stdout_pipe[1], STDOUT_FILENO) >= 0, "dup2 stdout failed");

    setenv("ACE_WORKER", "1", 1);
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    CHECK(pal != NULL, "pal_uv_create failed");
    ace_t *ace = ace_create(pal);
    CHECK(ace != NULL, "ace_create failed");

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    /* Drain init output */
    {
        char drain[4096];
        ssize_t n;
        while ((n = read(stdout_pipe[0], drain, sizeof(drain) - 1)) > 0) {}
    }

    /* Build a line > 64 KiB: {"jsonrpc":"2.0","method":"configure","params":{"api_key":"AAAA..."},"id":1}\n */
    size_t big_len = 70000;  /* > 65535 */
    const char *prefix = "{\"jsonrpc\":\"2.0\",\"method\":\"configure\",\"params\":{\"api_key\":\"";
    const char *suffix = "\"},\"id\":1}\n";
    size_t plen = strlen(prefix);
    size_t slen = strlen(suffix);
    size_t total_len = plen + big_len + slen;
    /* Allocate for the full prefix + fill + suffix (+1 NUL). The earlier
     * (big_len + 64) was too small — plen+slen > 64 — causing a fortify
     * buffer-overflow abort. */
    char *big = (char *)malloc(total_len + 1);
    CHECK(big != NULL, "malloc failed");
    memcpy(big, prefix, plen);
    /* fill with 'A' so the whole line exceeds 64 KiB */
    for (size_t i = 0; i < big_len; i++) big[plen + i] = 'A';
    memcpy(big + plen + big_len, suffix, slen);
    big[total_len] = '\0';

    size_t written = 0;
    /* Grow the pipe buffer so the whole 70 KiB message fits without the
     * worker draining mid-write (avoids a single-threaded write/tick
     * deadlock). F_SETPIPE_SZ is Linux-specific; on failure we fall back
     * to interleaved writes + ticks with a non-blocking read end. */
    int pipe_grown = (fcntl(stdin_pipe[1], F_SETPIPE_SZ, 256 * 1024) >= 0);
    while (written < total_len) {
        ssize_t n = write(stdin_pipe[1], big + written, total_len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (pipe_grown) {
                    /* Should not happen with a 256 KiB buffer, but yield. */
                    do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0);
                    continue;
                }
                /* Pipe full — drain via the worker (read end is blocking,
                 * but it returns data immediately since we wrote some). */
                ace_worker_tick(ace);
                ace_poll(ace, 0);
                continue;
            }
            break;
        }
        written += (size_t)n;
    }
    free(big);
    close(stdin_pipe[1]);

    /* Drive the worker until it has consumed and rejected the oversize line. */
    for (int i = 0; i < 50; i++) {
        ace_worker_tick(ace);
        ace_poll(ace, 0);
    }

    char response[4096];
    memset(response, 0, sizeof(response));
    ssize_t total = 0;
    int retries = 200;
    while (retries-- > 0 && total < (ssize_t)sizeof(response) - 1) {
        ssize_t n = read(stdout_pipe[0], response + total,
                         sizeof(response) - 1 - (size_t)total);
        if (n > 0) total += n;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0);
            /* keep ticking in case the worker hasn't flushed yet */
            ace_worker_tick(ace);
            ace_poll(ace, 0);
            continue;
        }
        else break;
    }
    response[total] = '\0';

    /* Must get an error response mentioning the oversize condition */
    CHECK(strlen(response) > 0, "expected JSON-RPC error response for oversize line");
    CHECK(strstr(response, "-32607") != NULL ||
          strstr(response, "too long") != NULL,
          "expected oversize error");

    close(stdout_pipe[0]);
    destroy_ace_isolated(ace, pal);

    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    unsetenv("ACE_WORKER");

    PASS();
}

/*
 * Regression: cancelling a run must release the run-active guard so a
 * subsequent 'run' is not permanently rejected with -32000 "A run is
 * already in progress". Uses an unreachable endpoint so the run fails
 * quickly; the point is that cancel frees the slot.
 */
static void test_worker_ipc_cancel_then_run(void) {
    TEST("worker cancel releases run slot for next run");

    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    CHECK(saved_stdin >= 0 && saved_stdout >= 0, "failed to save fds");

    int stdin_pipe[2], stdout_pipe[2];
    CHECK(pipe(stdin_pipe) == 0, "stdin pipe failed");
    CHECK(pipe(stdout_pipe) == 0, "stdout pipe failed");

    int flags;
    flags = fcntl(stdin_pipe[1], F_GETFL, 0);
    fcntl(stdin_pipe[1], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);
    /* non-blocking read end so we can interleave writes and ticks */
    flags = fcntl(stdin_pipe[0], F_GETFL, 0);
    fcntl(stdin_pipe[0], F_SETFL, flags | O_NONBLOCK);

    CHECK(dup2(stdin_pipe[0], STDIN_FILENO) >= 0, "dup2 stdin failed");
    CHECK(dup2(stdout_pipe[1], STDOUT_FILENO) >= 0, "dup2 stdout failed");

    setenv("ACE_WORKER", "1", 1);
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    CHECK(pal != NULL, "pal_uv_create failed");
    ace_t *ace = ace_create(pal);
    CHECK(ace != NULL, "ace_create failed");

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    /* Drain init output */
    {
        char drain[4096];
        ssize_t n;
        while ((n = read(stdout_pipe[0], drain, sizeof(drain) - 1)) > 0) {}
    }

    /* Configure with an unreachable endpoint */
    const char *cfg = "{\"jsonrpc\":\"2.0\",\"method\":\"configure\","
        "\"params\":{\"api_key\":\"sk-test\",\"base_url\":\"http://127.0.0.1:1/v1\","
        "\"model\":\"gpt-4\"},\"id\":1}\n";
    CHECK(write_all(stdin_pipe[1], cfg, strlen(cfg)) == 0, "write cfg failed");
    ace_worker_tick(ace);
    ace_poll(ace, 0);

    /* Start a run, then immediately cancel it */
    const char *run1 = "{\"jsonrpc\":\"2.0\",\"method\":\"run\","
        "\"params\":{\"input\":\"hi\"},\"id\":2}\n";
    CHECK(write_all(stdin_pipe[1], run1, strlen(run1)) == 0, "write run1 failed");
    ace_worker_tick(ace);

    const char *cancel = "{\"jsonrpc\":\"2.0\",\"method\":\"cancel\",\"id\":3}\n";
    CHECK(write_all(stdin_pipe[1], cancel, strlen(cancel)) == 0, "write cancel failed");
    ace_worker_tick(ace);
    ace_poll(ace, 0);

    /* Drain whatever the cancelled run produced */
    {
        char drain[4096];
        ssize_t n;
        for (int i = 0; i < 50; i++) {
            ace_worker_tick(ace);
            ace_poll(ace, 0);
            while ((n = read(stdout_pipe[0], drain, sizeof(drain) - 1)) > 0) {}
        }
    }

    /* Second run must NOT be rejected with -32000 'in progress' */
    const char *run2 = "{\"jsonrpc\":\"2.0\",\"method\":\"run\","
        "\"params\":{\"input\":\"hi again\"},\"id\":4}\n";
    CHECK(write_all(stdin_pipe[1], run2, strlen(run2)) == 0, "write run2 failed");

    /* Drive so the run is dispatched */
    ace_worker_tick(ace);
    ace_poll(ace, 0);

    /* Read responses and assert no -32000 in-progress rejection */
    char response[8192];
    memset(response, 0, sizeof(response));
    ssize_t total = 0;
    int retries = 300;
    while (retries-- > 0 && total < (ssize_t)sizeof(response) - 1) {
        ssize_t n = read(stdout_pipe[0], response + total,
                         sizeof(response) - 1 - (size_t)total);
        if (n > 0) total += n;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            ace_worker_tick(ace);
            ace_poll(ace, 0);
            do { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); } while(0);
            continue;
        }
        else break;
    }
    response[total] = '\0';

    CHECK(strstr(response, "-32000") == NULL,
          "second run must not be rejected as 'already in progress'");

    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    destroy_ace_isolated(ace, pal);

    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    unsetenv("ACE_WORKER");

    PASS();
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    fprintf(stderr, "ACE Core C API Tests\n");
    fprintf(stderr, "====================\n\n");

    /* Version (no ace needed) */
    test_version_string();
    test_version_components();
    test_version_components_null();

    /* Lifecycle (no ace needed) */
    test_create_null_pal();
    test_destroy_null();

    /* Configuration */
    test_set_api_key();
    test_set_api_key_null_ace();
    test_set_base_url();
    test_set_model();

    /* Tool registration */
    test_register_tool();
    test_register_tool_duplicate();
    test_register_tool_null_ace();
    test_register_tool_null_name();
    test_register_async_tool();
    test_register_js_tool();
    test_register_js_tool_null_ace();
    test_register_js_tool_execution();
    test_register_js_tool_null_name();

    /* Run lifecycle */
    test_run_null_ace();
    test_run_null_input();
    test_run_null_cbs();

    /* Poll */
    test_poll_null_ace();
    test_poll_idle();
    test_poll_error_code();

    /* History */
    test_clear_history_null();
    test_get_history_json_null();
    test_clear_history();
    test_get_history_json_empty();

    /* Error */
    test_last_error_null();
    test_last_error_initial();
    test_last_error_code();

    /* Reset */
    test_reset_clears_state();
    test_reset_null_ace();

    /* Cancellation */
    test_cancel_null();
    test_cancel_idle();
    test_cancel_during_poll();
    test_stale_cancel_does_not_cancel_next_run();
    test_on_done_fires_once();
    test_cancel_aborts_http_stream();
    test_cancel_from_another_thread();

    /* Async tool */
    test_provide_tool_result_null();
    test_provide_tool_result_no_pending();

    /* Worker mode */
    test_worker_tick_null();
    test_worker_tick_non_worker();
    test_worker_ipc_configure();
    test_worker_ipc_shutdown();
    test_worker_ipc_unknown_method();
    test_worker_ipc_unknown_tool_result();
    test_worker_ipc_oversize_line();
    test_worker_ipc_cancel_then_run();

    /* Free */
    test_ace_free_null();

    /* Summary */
    fprintf(stderr, "\n====================\n");
    fprintf(stderr, "Results: %d/%d passed, %d failed\n",
        tests_passed, tests_run, tests_failed);

    /* Cleanup shared ace + pal directly (the uv_close assertion that
     * previously required fork isolation is fixed — pal_uv_http_cleanup
     * now untracks all op handles before freeing). */
    if (g_ace) ace_destroy(g_ace);
    if (g_pal) pal_uv_destroy(g_pal);

    return tests_failed > 0 ? 1 : 0;
}
