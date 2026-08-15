// test_wasm_streaming_gtest.cpp — Task 3: WebAssembly.compileStreaming /
// instantiateStreaming（v1 语义等价：source 提供 arrayBuffer()，取完整字节
// 后交 compile/instantiate）。
#include "test_host.h"

// 最小加法模块：wat2wasm 生成，导出 add(i32,i32)->i32（local.get 0; local.get 1; i32.add）
static const unsigned char kAddModule[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x07, 0x01, 0x60,
    0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00, 0x07, 0x07, 0x01,
    0x03, 0x61, 0x64, 0x64, 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x20,
    0x00, 0x20, 0x01, 0x6a, 0x0b
};

// JS 侧构造 streaming source：{ arrayBuffer: () => Promise.resolve(bytes) }
static std::string kStreamingSource = [] {
    std::string s = "var _bytes = new Uint8Array([";
    for (size_t i = 0; i < sizeof(kAddModule); i++) {
        if (i) s += ",";
        s += std::to_string(kAddModule[i]);
    }
    s += "]);\n";
    s += "var _src = { arrayBuffer: function(){ return Promise.resolve(_bytes.buffer); } };";
    return s;
}();

TEST(wasm_streaming, compileStreaming_returns_module) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h, (kStreamingSource +
        "\nvar _add = null;\n"
        "WebAssembly.compileStreaming(_src)\n"
        "  .then(function(mod){ return new WebAssembly.Instance(mod); })\n"
        "  .then(function(inst){ _add = inst.exports.add(1, 2); })\n"
        "  .catch(function(e){ _add = 'ERR:' + e.message; });\n"
        "0").c_str(), &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_add)", "3", &v))
        << "compileStreaming did not produce add(1,2)==3, got: " << v;
    host_destroy(h);
}

TEST(wasm_streaming, instantiateStreaming_returns_instance) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h, (kStreamingSource +
        "\nvar _add = null;\n"
        "WebAssembly.instantiateStreaming(_src)\n"
        "  .then(function(r){ _add = r.instance.exports.add(20, 22); })\n"
        "  .catch(function(e){ _add = 'ERR:' + e.message; });\n"
        "0").c_str(), &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_add)", "42", &v))
        << "instantiateStreaming did not produce add(20,22)==42, got: " << v;
    host_destroy(h);
}

TEST(wasm_streaming, source_without_arrayBuffer_rejects) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _err = null;\n"
        "WebAssembly.compileStreaming({})\n"
        "  .then(function(){ _err = 'unexpected resolve'; })\n"
        "  .catch(function(e){ _err = e.message; });\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "String(_err)", "arrayBuffer", &v))
        << "expected TypeError about arrayBuffer, got: " << v;
    host_destroy(h);
}
