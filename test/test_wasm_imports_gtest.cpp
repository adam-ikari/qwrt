// test_wasm_imports_gtest.cpp — WASM importObject support.
//
// Covers: function imports bridged to JS (i32/f64/i64/void), missing-import
// errors, non-object importObject, WebAssembly.instantiate(buffer, imports),
// and JS-exception propagation out of an imported function.
//
// Fixtures were produced with wat2wasm:
//   add      — (import "env" "add" (func (param i32 i32) (result i32))),
//              exports run(i32,i32)->i32 (calls add) and double()->i32
//   combine  — (import "env" "combine" (func (param i64 f64) (result f64))),
//              exports run(i64,f64)->f64 (calls combine)
//   log      — (import "env" "log" (func (param i32))),
//              exports run(i32) (calls log)
//   memimport— (import "env" "mem" (memory 1)), exports size() — memory
//              imports are a documented WAMR limitation (clear error).
//
// Teardown notes (pre-existing engine behavior, not import-specific):
//   * A WebAssembly.Module/Instance kept alive as a JS *global* leaks a
//     reference at runtime teardown.
//   * An Instance whose only JS reference is an inline temporary can be GC'd
//     in the middle of an exported call (exported function closures do not
//     hold the instance), dangling the WAMR instance.
// Tests therefore create the instance in an IIFE-local (alive during the
// call, garbage afterward) and retain only scalar results.
#include "test_host.h"

// Turn a wasm byte array into a JS expression creating an ArrayBuffer.
static std::string wasm_js(const char *name, const unsigned char *bytes, size_t len) {
    std::string s = std::string("var ") + name + " = new Uint8Array([";
    for (size_t i = 0; i < len; i++) {
        if (i) s += ",";
        s += std::to_string(bytes[i]);
    }
    s += "]).buffer;\n";
    return s;
}

// add.wasm
static const unsigned char kAddModule[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0b, 0x02, 0x60,
    0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x60, 0x00, 0x01, 0x7f, 0x02, 0x0b, 0x01,
    0x03, 0x65, 0x6e, 0x76, 0x03, 0x61, 0x64, 0x64, 0x00, 0x00, 0x03, 0x03,
    0x02, 0x00, 0x01, 0x07, 0x10, 0x02, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x01,
    0x06, 0x64, 0x6f, 0x75, 0x62, 0x6c, 0x65, 0x00, 0x02, 0x0a, 0x13, 0x02,
    0x08, 0x00, 0x20, 0x00, 0x20, 0x01, 0x10, 0x00, 0x0b, 0x08, 0x00, 0x41,
    0x15, 0x41, 0x15, 0x10, 0x00, 0x0b
};

// combine.wasm
static const unsigned char kCombineModule[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x07, 0x01, 0x60,
    0x02, 0x7e, 0x7c, 0x01, 0x7c, 0x02, 0x0f, 0x01, 0x03, 0x65, 0x6e, 0x76,
    0x07, 0x63, 0x6f, 0x6d, 0x62, 0x69, 0x6e, 0x65, 0x00, 0x00, 0x03, 0x02,
    0x01, 0x00, 0x07, 0x07, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x01, 0x0a,
    0x0a, 0x01, 0x08, 0x00, 0x20, 0x00, 0x20, 0x01, 0x10, 0x00, 0x0b
};

// log.wasm
static const unsigned char kLogModule[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60,
    0x01, 0x7f, 0x00, 0x02, 0x0b, 0x01, 0x03, 0x65, 0x6e, 0x76, 0x03, 0x6c,
    0x6f, 0x67, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x07, 0x07, 0x01, 0x03,
    0x72, 0x75, 0x6e, 0x00, 0x01, 0x0a, 0x08, 0x01, 0x06, 0x00, 0x20, 0x00,
    0x10, 0x00, 0x0b
};

// memimport.wasm
static const unsigned char kMemImportModule[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60,
    0x00, 0x01, 0x7f, 0x02, 0x0c, 0x01, 0x03, 0x65, 0x6e, 0x76, 0x03, 0x6d,
    0x65, 0x6d, 0x02, 0x00, 0x01, 0x03, 0x02, 0x01, 0x00, 0x07, 0x08, 0x01,
    0x04, 0x73, 0x69, 0x7a, 0x65, 0x00, 0x00, 0x0a, 0x06, 0x01, 0x04, 0x00,
    0x3f, 0x00, 0x0b
};

static const std::string kAdd = wasm_js("_addBuf", kAddModule, sizeof(kAddModule));
static const std::string kCombine = wasm_js("_cbBuf", kCombineModule, sizeof(kCombineModule));
static const std::string kLog = wasm_js("_logBuf", kLogModule, sizeof(kLogModule));
static const std::string kMemImport = wasm_js("_memBuf", kMemImportModule, sizeof(kMemImportModule));

// Helper: run JS, expect no exception (bootstrap returns {ok:true}).
static void expect_eval_ok(HostCtx *h, const std::string &code) {
    std::string out;
    ASSERT_TRUE(host_eval(h, code.c_str(), &out)) << "eval failed";
    EXPECT_TRUE(out.find("\"ok\":true") != std::string::npos)
        << "unexpected eval response: " << out;
}

TEST(wasm_imports, instance_calls_import_function) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    expect_eval_ok(h, kAdd +
        "(function(){ var mod = new WebAssembly.Module(_addBuf);\n"
        "  var inst = new WebAssembly.Instance(mod, {env:{add:function(a,b){return a+b;}}});\n"
        "  _r1 = inst.exports.run(3, 4);\n"
        "  _r2 = inst.exports.double();\n"
        "})();\n"
        "0");
    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_r1)", "7", &v))
        << "run(3,4) did not reach the host add(), got: " << v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_r2)", "42", &v))
        << "double() did not use the imported add(), got: " << v;
    host_destroy(h);
}

TEST(wasm_imports, instantiate_with_import_object) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    expect_eval_ok(h, kAdd +
        "WebAssembly.instantiate(_addBuf, {env:{add:function(a,b){return a*b;}}})\n"
        "  .then(function(r){ _r = r.instance.exports.run(3, 4); })\n"
        "  .catch(function(e){ _r = 'ERR:' + e.message; });\n"
        "0");
    std::string v;
    // Multiplication proves the importObject binding is actually used.
    ASSERT_TRUE(host_poll_until_value(h, "String(_r)", "12", &v))
        << "instantiate(buffer, imports) did not use host add(), got: " << v;
    host_destroy(h);
}

TEST(wasm_imports, import_i64_f64_conversion) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    expect_eval_ok(h, kCombine +
        "var _kind = null;\n"
        "(function(){ var mod = new WebAssembly.Module(_cbBuf);\n"
        "  var inst = new WebAssembly.Instance(mod, {env:{combine:function(a,b){_kind = typeof a; return Number(a) + b;}}});\n"
        "  _r = inst.exports.run(10, 0.25);\n"
        "})();\n"
        "0");
    std::string v;
    // The i64 WASM argument must reach JS as a BigInt.
    ASSERT_TRUE(host_poll_until_value(h, "String(_kind)", "bigint", &v))
        << "i64 import arg was not converted to BigInt, got: " << v;
    // And the f64 result round-trips back to JS.
    ASSERT_TRUE(host_poll_until_value(h, "String(_r)", "10.25", &v))
        << "combine() f64 result wrong, got: " << v;
    host_destroy(h);
}

TEST(wasm_imports, void_import_side_effect) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    expect_eval_ok(h, kLog +
        "var _log = null;\n"
        "(function(){ var mod = new WebAssembly.Module(_logBuf);\n"
        "  var inst = new WebAssembly.Instance(mod, {env:{log:function(v){_log = v;}}});\n"
        "  inst.exports.run(42);\n"
        "})();\n"
        "0");
    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_log)", "42", &v))
        << "void import did not observe its argument, got: " << v;
    host_destroy(h);
}

TEST(wasm_imports, missing_import_throws) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    expect_eval_ok(h, kAdd +
        "var _err = null;\n"
        "try { new WebAssembly.Instance(new WebAssembly.Module(_addBuf), {}); }"
        "catch(e) { _err = e.message; }\n"
        "0");
    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_err)", "import", &v))
        << "expected a missing-import error, got: " << v;
    host_destroy(h);
}

TEST(wasm_imports, missing_import_module_throws) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    expect_eval_ok(h, kAdd +
        "var _err = null;\n"
        "try { new WebAssembly.Instance(new WebAssembly.Module(_addBuf), "
        "{other:{add:function(a,b){return a+b;}}}); }"
        "catch(e) { _err = e.message; }\n"
        "0");
    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_err)", "import", &v))
        << "expected a missing-import error, got: " << v;
    host_destroy(h);
}

TEST(wasm_imports, missing_import_object_throws) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    expect_eval_ok(h, kAdd +
        "var _err = null;\n"
        "try { new WebAssembly.Instance(new WebAssembly.Module(_addBuf)); }"
        "catch(e) { _err = e.message; }\n"
        "0");
    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_err)", "import", &v))
        << "expected an error about the missing importObject, got: " << v;
    host_destroy(h);
}

TEST(wasm_imports, non_object_import_object_throws) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    expect_eval_ok(h, kAdd +
        "var _err = null;\n"
        "try { new WebAssembly.Instance(new WebAssembly.Module(_addBuf), 42); }"
        "catch(e) { _err = e.message; }\n"
        "0");
    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_err)", "object", &v))
        << "expected an error about the non-object importObject, got: " << v;
    host_destroy(h);
}

TEST(wasm_imports, import_exception_propagates) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    expect_eval_ok(h, kAdd +
        "var _err = null;\n"
        "(function(){ var mod = new WebAssembly.Module(_addBuf);\n"
        "  var inst = new WebAssembly.Instance(mod, {env:{add:function(){throw new Error('boom-import');}}});\n"
        "  try { inst.exports.run(1, 2); } catch(e) { _err = e.message; }\n"
        "})();\n"
        "_ok = (_err.indexOf('boom') >= 0 || _err.indexOf('threw') >= 0);\n"
        "0");
    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_ok)", "true", &v))
        << "the JS exception from the import was not propagated, got: " << v;
    host_destroy(h);
}

TEST(wasm_imports, memory_import_is_unsupported) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    expect_eval_ok(h, kMemImport +
        "var _err = null;\n"
        "try { new WebAssembly.Instance(new WebAssembly.Module(_memBuf), {env:{}}); }"
        "catch(e) { _err = e.message; }\n"
        "0");
    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_err)", "import", &v))
        << "expected a clear error for the unsupported memory import, got: " << v;
    host_destroy(h);
}
