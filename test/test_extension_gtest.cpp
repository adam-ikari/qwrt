/*
 * qwrt Extension Tests (Google Test)
 *
 * Tests for extension lifecycle: init, destroy, suspend, resume hooks,
 * and interaction with reset and context operations.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <quickjs.h>
}

/* ================================================================
 * Minimal test polyfill
 * ================================================================ */

static const char test_polyfill[] =
    "(function(pal) {\n"
    "  globalThis.testHelper = {\n"
    "    storageSet: function(k, v) { return pal.storageSet(k, v); },\n"
    "    storageGet: function(k) { return pal.storageGet(k); },\n"
    "    timeNow: function() { return pal.timeNow(); },\n"
    "    log: function(level, msg) { pal.log(level, msg); },\n"
    "  };\n"
    "  globalThis.console = {\n"
    "    log: function() { pal.log(0, Array.from(arguments).join(' ')); },\n"
    "    error: function() { pal.log(2, Array.from(arguments).join(' ')); },\n"
    "  };\n"
    "  globalThis.performance = {\n"
    "    now: function() { return pal.timeNow(); },\n"
    "  };\n"
    "})(__pal__);";

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
    (void)rt;
    test_ext_state_t *state = (test_ext_state_t *)ext->user_data;
    state->destroy_called++;
}

static int test_ext_suspend(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)rt;
    test_ext_state_t *state = (test_ext_state_t *)ext->user_data;
    state->suspend_called++;
    return 0;
}

static int test_ext_resume(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)rt;
    test_ext_state_t *state = (test_ext_state_t *)ext->user_data;
    state->resume_called++;
    return 0;
}

static int test_ext_init_fail(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)rt;
    test_ext_state_t *state = (test_ext_state_t *)ext->user_data;
    state->init_called++;
    return -1;
}

static qwrt_ext_t *make_test_ext(const char *name, test_ext_state_t *state)
{
    memset(state, 0, sizeof(test_ext_state_t));
    snprintf(state->name, sizeof(state->name), "%s", name);

    static qwrt_ext_t ext;
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
 * Helper
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

TEST(QwrtExtension, InitDestroy) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("test_ext", &state);
    const qwrt_ext_t *exts[] = { ext, nullptr };

    qwrt_config_t config;
    fill_test_config(&config, pal, exts);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    EXPECT_EQ(state.init_called, 1);
    EXPECT_EQ(state.destroy_called, 0);

    char *result = nullptr;
    int rc = qwrt_eval(rt, "__ext_name__", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"test_ext\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    EXPECT_EQ(state.destroy_called, 1);

    pal_mock_destroy(pal);
}

TEST(QwrtExtension, SuspendResume) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("sr_ext", &state);
    const qwrt_ext_t *exts[] = { ext, nullptr };

    qwrt_config_t config;
    fill_test_config(&config, pal, exts);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);
    EXPECT_EQ(state.init_called, 1);

    int rc = qwrt_suspend(rt);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(state.suspend_called, 1);
    EXPECT_EQ(state.resume_called, 0);

    rc = qwrt_resume(rt, 0);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(state.resume_called, 1);

    qwrt_destroy(rt);
    EXPECT_EQ(state.destroy_called, 1);

    pal_mock_destroy(pal);
}

TEST(QwrtExtension, ResetCallsDestroyInit) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("reset_ext", &state);
    const qwrt_ext_t *exts[] = { ext, nullptr };

    qwrt_config_t config;
    fill_test_config(&config, pal, exts);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);
    EXPECT_EQ(state.init_called, 1);
    EXPECT_EQ(state.destroy_called, 0);

    int rc = qwrt_reset(rt, &config);
    EXPECT_EQ(rc, 0);

    EXPECT_EQ(state.destroy_called, 1);
    EXPECT_EQ(state.init_called, 2);

    char *result = nullptr;
    rc = qwrt_eval(rt, "__ext_name__", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"reset_ext\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    EXPECT_EQ(state.destroy_called, 2);

    pal_mock_destroy(pal);
}

TEST(QwrtExtension, NoExtensions) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal, nullptr);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *result = nullptr;
    int rc = qwrt_eval(rt, "1 + 1", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);

    rc = qwrt_eval(rt, "typeof __ext_name__", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"undefined\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtExtension, RegisterExtAfterCreate) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal, nullptr);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *result = nullptr;
    int rc = qwrt_eval(rt, "typeof __ext_name__", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"undefined\"");
    qwrt_free(result);

    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("runtime_ext", &state);
    rc = qwrt_register_ext(rt, ext);
    EXPECT_EQ(rc, 0);

    EXPECT_EQ(state.init_called, 1);

    rc = qwrt_eval(rt, "__ext_name__", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"runtime_ext\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    EXPECT_EQ(state.destroy_called, 1);

    pal_mock_destroy(pal);
}

TEST(QwrtExtension, RegisterExtSuspendResume) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal, nullptr);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("sr_reg", &state);
    int rc = qwrt_register_ext(rt, ext);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(state.init_called, 1);

    rc = qwrt_suspend(rt);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(state.suspend_called, 1);

    rc = qwrt_resume(rt, 0);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(state.resume_called, 1);

    qwrt_destroy(rt);
    EXPECT_EQ(state.destroy_called, 1);

    pal_mock_destroy(pal);
}

TEST(QwrtExtension, RegisterExtInitFailure) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal, nullptr);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    test_ext_state_t state;
    qwrt_ext_t *ext = make_test_ext("fail_ext", &state);
    ext->init = test_ext_init_fail;

    int rc = qwrt_register_ext(rt, ext);
    EXPECT_EQ(rc, -1);

    EXPECT_EQ(state.destroy_called, 0);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}
