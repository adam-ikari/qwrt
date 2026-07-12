/*
 * qwrt Extension Tests (Google Test)
 *
 * The extension set is fixed at build time via the QWRT_EXTENSIONS macro
 * (qwrt_ext_registry.h) - there is no runtime registration. These tests
 * verify the built-in default extensions (compress/crypto/textcodec/wasm3)
 * are registered and that their lifecycle (init/destroy via create/
 * destroy/reset) works, plus that a build with no extensions behaves.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "qwrt/qwrt.h"
#include "qwrt/qwrt_ext_registry.h"
#include "pal_mock.h"
}

/* ================================================================
 * Helpers
 * ================================================================ */

static void fill_test_config(qwrt_config_t *config, const qwrt_pal_t *pal)
{
    memset(config, 0, sizeof(*config));
    config->pal = pal;
}

/* Whether a given extension is compiled into this build (its QWRT_WITH_* on).
 * The QWRT_EXTENSIONS table still references it as a non-NULL entry only when
 * it is compiled in; a disabled built-in is a NULL slot. */
static int ext_compiled_in(const char *name)
{
#if QWRT_WITH_COMPRESS
    if (strcmp(name, "compress") == 0) return 1;
#endif
#if QWRT_WITH_CRYPTO_EXT
    if (strcmp(name, "crypto") == 0) return 1;
#endif
#if QWRT_WITH_TEXTCODEC
    if (strcmp(name, "textcodec") == 0) return 1;
#endif
#if QWRT_WITH_WASM3
    if (strcmp(name, "wasm3") == 0) return 1;
#endif
    (void)name;
    return 0;
}

/* ================================================================
 * Tests
 * ================================================================ */

/* A runtime is creatable with the default (build-time) extension set and
 * survives create/destroy. The default polyfill is injected (eval works). */
TEST(QwrtExtension, CreateDestroyDefaultSet) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *result = nullptr;
    int rc = qwrt_eval(rt, "1 + 1", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* The default extension set registers the compiled-in built-ins. Check the
 * JS-visible surface each one adds (crypto.subtle, CompressionStream,
 * TextEncoder, WebAssembly). */
TEST(QwrtExtension, BuiltinsRegistered) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *r = nullptr;
    /* typeof X returns a JSON-quoted string, e.g. "\"object\"". Only assert
     * "defined" when the native ext is compiled in - when it's NOT compiled,
     * the polyfill may still provide a JS fallback surface (e.g. a JS
     * CompressionStream), so we don't assert undefined for the not-compiled
     * case (that'd be implementation-dependent and fragile). */
    auto check_defined = [&](const char *expr) {
        qwrt_eval(rt, expr, &r);
        ASSERT_NE(r, nullptr) << expr;
        EXPECT_STRNE(r, "\"undefined\"") << expr << " should be defined (compiled in)";
        qwrt_free(r);
        r = nullptr;
    };

    if (ext_compiled_in("crypto"))    check_defined("typeof crypto.subtle");
    if (ext_compiled_in("compress"))  check_defined("typeof CompressionStream");
    if (ext_compiled_in("textcodec")) check_defined("typeof TextEncoder");
    /* WebAssembly: registered when a WASM engine is compiled in. */
    if (ext_compiled_in("wasm3"))     check_defined("typeof WebAssembly");

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* Reset re-runs destroy+init for the extension set (the default polyfill is
 * re-injected; built-ins re-init). Verify eval still works post-reset. */
TEST(QwrtExtension, ResetReinitializesExtensions) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    int rc = qwrt_reset(rt, &config);
    EXPECT_EQ(rc, 0);

    char *result = nullptr;
    rc = qwrt_eval(rt, "2 + 2", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "4");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* suspend/resume cycle over the whole extension set doesn't break the
 * runtime. */
TEST(QwrtExtension, SuspendResumeDefaultSet) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    int rc = qwrt_suspend(rt);
    EXPECT_EQ(rc, 0);
    rc = qwrt_resume(rt, 0);
    EXPECT_EQ(rc, 0);

    char *result = nullptr;
    rc = qwrt_eval(rt, "5 + 5", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "10");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}
