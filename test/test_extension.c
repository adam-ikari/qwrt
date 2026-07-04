/*
 * qwrt Extension Tests
 *
 * Tests for extension lifecycle: init, destroy, suspend, resume hooks,
 * and interaction with reset and context operations.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <quickjs.h>

/* ================================================================
 * Minimal test framework (same as test_qwrt.h)
 * ================================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)

#define ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s (line %d)\n", #expr, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL: \"%s\" != \"%s\" (line %d)\n", (a), (b), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))

#define RUN_TEST(name) do { \
    int prev_failed = tests_failed; \
    printf("  %s...", #name); \
    tests_run++; \
    test_##name(); \
    if (tests_failed == prev_failed) { \
        printf(" PASS\n"); \
        tests_passed++; \
    } else { \
        printf("\n"); \
    } \
} while(0)

/* testHelper is provided by the default polyfill via __pal__ */

/* ================================================================
 * Test extension — tracks init/destroy/suspend/resume calls
 * ================================================================ */

typedef struct {
    int init_called;
    int destroy_called;
    int suspend_called;
    int resume_called;
    char name[32];
} test_ext_state_t;

static int test_ext_init(qwrt_ext_t *ext, qwrt_t *rt)
{
    test_ext_state_t *state = (test_ext_state_t *)ext->user_data;
    state->init_called++;

    /* Register __ext_name__ global via qwrt_get_jsctx */
    JSContext *jsctx = (JSContext *)qwrt_get_jsctx(rt);
    if (jsctx) {
        JSValue global = JS_GetGlobalObject(jsctx);
        JS_SetPropertyStr(jsctx, global, "__ext_name__",
                           JS_NewString(jsctx, state->name));
        JS_FreeValue(jsctx, global);
    }

    return 0;
}

static void test_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt)
{
    test_ext_state_t *state = (test_ext_state_t *)ext->user_data;
    state->destroy_called++;
}

static int test_ext_suspend(qwrt_ext_t *ext, qwrt_t *rt)
{
    test_ext_state_t *state = (test_ext_state_t *)ext->user_data;
    state->suspend_called++;
    return 0;
}

static int test_ext_resume(qwrt_ext_t *ext, qwrt_t *rt)
{
    test_ext_state_t *state = (test_ext_state_t *)ext->user_data;
    state->resume_called++;
    return 0;
}

static int test_ext_init_fail(qwrt_ext_t *ext, qwrt_t *rt)
{
    test_ext_state_t *state = (test_ext_state_t *)ext->user_data;
    state->init_called++;
    return -1;  /* Always fail */
}

/* Helper: create a test extension with given name */
static qwrt_ext_t *make_test_ext(const char *name, test_ext_state_t *state)
{
    memset(state, 0, sizeof(test_ext_state_t));
    snprintf(state->name, sizeof(state->name), "%s", name);

    static qwrt_ext_t ext;  /* static so pointer remains valid */
    memset(&ext, 0, sizeof(ext));
    ext.name = state->name;
    ext.version = 1;
    ext.init = test_ext_init;
    ext.destroy = test_ext_destroy;
    ext.suspend = test_ext_suspend;
    ext.resume = test_ext_resume;
    ext.user_data = state;

    return &ext;
}

/* ================================================================
 * Helper: fill a qwrt_config_t with test defaults
 * ================================================================ */

static void fill_test_config(qwrt_config_t *config, const qwrt_pal_t *pal,
                              const qwrt_ext_t **extensions)
{
    memset(config, 0, sizeof(*config));
    config->pal = pal;
    config->extensions = extensions;
}

/* ================================================================
 * Tests
 * ================================================================ */

TEST(ext_init_destroy) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("test_ext", &state);
    const qwrt_ext_t *exts[] = { ext, NULL };

    qwrt_config_t config;
    fill_test_config(&config, pal, exts);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* init should have been called once */
    ASSERT_EQ(state.init_called, 1);
    ASSERT_EQ(state.destroy_called, 0);

    /* Extension should have registered __ext_name__ global */
    char *result = NULL;
    int rc = qwrt_eval(rt, "__ext_name__", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"test_ext\"");
    qwrt_free(result);

    /* Destroy runtime — destroy hook should be called */
    qwrt_destroy(rt);
    ASSERT_EQ(state.destroy_called, 1);

    pal_mock_destroy(pal);
}

TEST(ext_suspend_resume) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("sr_ext", &state);
    const qwrt_ext_t *exts[] = { ext, NULL };

    qwrt_config_t config;
    fill_test_config(&config, pal, exts);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);
    ASSERT_EQ(state.init_called, 1);

    /* Suspend — should call suspend hook */
    int rc = qwrt_suspend(rt);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(state.suspend_called, 1);
    ASSERT_EQ(state.resume_called, 0);

    /* Resume ctx0 — should call resume hook */
    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(state.resume_called, 1);

    qwrt_destroy(rt);
    ASSERT_EQ(state.destroy_called, 1);

    pal_mock_destroy(pal);
}

TEST(ext_reset_calls_destroy_init) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("reset_ext", &state);
    const qwrt_ext_t *exts[] = { ext, NULL };

    qwrt_config_t config;
    fill_test_config(&config, pal, exts);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);
    ASSERT_EQ(state.init_called, 1);
    ASSERT_EQ(state.destroy_called, 0);

    /* Reset — should call destroy on old context, then init on new */
    int rc = qwrt_reset(rt, &config);
    ASSERT_EQ(rc, 0);

    /* destroy called for old context, init called for new context */
    ASSERT_EQ(state.destroy_called, 1);
    ASSERT_EQ(state.init_called, 2);

    /* Verify new context has the extension global */
    char *result = NULL;
    rc = qwrt_eval(rt, "__ext_name__", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"reset_ext\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    /* destroy called again on final qwrt_destroy */
    ASSERT_EQ(state.destroy_called, 2);

    pal_mock_destroy(pal);
}

TEST(ext_no_extensions) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal, NULL);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Runtime should work fine with no extensions */
    char *result = NULL;
    int rc = qwrt_eval(rt, "1 + 1", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "2");
    qwrt_free(result);

    /* __ext_name__ should not exist */
    rc = qwrt_eval(rt, "typeof __ext_name__", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"undefined\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * qwrt_register_ext tests
 * ================================================================ */

TEST(register_ext_after_create) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal, NULL);  /* No extensions at create time */

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* No extension global should exist */
    char *result = NULL;
    int rc = qwrt_eval(rt, "typeof __ext_name__", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"undefined\"");
    qwrt_free(result);

    /* Register extension at runtime */
    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("runtime_ext", &state);
    rc = qwrt_register_ext(rt, ext);
    ASSERT_EQ(rc, 0);

    /* Init should have been called */
    ASSERT_EQ(state.init_called, 1);

    /* Extension global should now exist */
    rc = qwrt_eval(rt, "__ext_name__", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"runtime_ext\"");
    qwrt_free(result);

    /* Destroy should call ext destroy */
    qwrt_destroy(rt);
    ASSERT_EQ(state.destroy_called, 1);

    pal_mock_destroy(pal);
}

TEST(register_ext_suspend_resume) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal, NULL);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("sr_reg", &state);
    int rc = qwrt_register_ext(rt, ext);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(state.init_called, 1);

    /* Suspend/resume should call extension hooks */
    rc = qwrt_suspend(rt);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(state.suspend_called, 1);

    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(state.resume_called, 1);

    qwrt_destroy(rt);
    ASSERT_EQ(state.destroy_called, 1);

    pal_mock_destroy(pal);
}

TEST(register_ext_init_failure) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal, NULL);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Create an extension whose init always fails */
    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("fail_ext", &state);
    ext->init = test_ext_init_fail;

    int rc = qwrt_register_ext(rt, ext);
    ASSERT_EQ(rc, -1);

    /* Destroy should NOT call ext destroy since init failed */
    ASSERT_EQ(state.destroy_called, 0);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("=== qwrt Extension Tests ===\n\n");

    printf("--- init/destroy ---\n");
    RUN_TEST(ext_init_destroy);

    printf("\n--- suspend/resume ---\n");
    RUN_TEST(ext_suspend_resume);

    printf("\n--- reset ---\n");
    RUN_TEST(ext_reset_calls_destroy_init);

    printf("\n--- no extensions ---\n");
    RUN_TEST(ext_no_extensions);

    printf("\n--- qwrt_register_ext ---\n");
    RUN_TEST(register_ext_after_create);
    RUN_TEST(register_ext_suspend_resume);
    RUN_TEST(register_ext_init_failure);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}