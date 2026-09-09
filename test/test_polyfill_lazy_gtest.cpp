// test_polyfill_lazy_gtest.cpp — polyfill 惰性加载透明性契约（P0 机制验证）
//
// 覆盖 lazy 化模块的"消费者无感契约"（设计文档 §2.5）：
//   1. 属性可见性：首次访问前 `name in globalThis` / typeof / Object.keys /
//      getOwnPropertyNames 均包含该名（enumerable:true），descriptor 为
//      configurable+enumerable 的 accessor——不存在"属性缺失"中间态。
//   2. 首次访问后：属性退化为数据属性，writable/enumerable/configurable 与
//      eager 安装逐位一致；再次访问不再触发副作用（身份稳定、引用恒定）。
//   3. 删除语义：delete 后属性消失且不复活。
//   4. 宿主对象子属性（qwrt.fs / qwrt.storage）：宿主对象 eager 空壳，
//      子属性 accessor 化，`in` / keys 语义与 eager 一致。
//   5. 表面等价：期望 descriptor/typeof 表 vs lazy 构建实际值——差异为空。
//
// 每个 TEST_F 用独立 runtime（host_create/host_destroy），避免跨测试状态泄漏。
#include "test_host.h"
#include <cstring>
#include <string>

namespace {

class PolyfillLazyTest : public ::testing::Test {
protected:
    HostCtx *h = nullptr;

    void SetUp() override {
        h = host_create();
        ASSERT_NE(nullptr, h);
    }
    void TearDown() override { host_destroy(h); }
};

// 对全局名做"访问前/后"两步探测：
//   pre:  in / keys / getOwnPropertyNames / 是否 accessor（get,无 value）/
//         enumerable / configurable
//   post: typeof（触发一次 setup）/ 是否 data property / writable / enumerable /
//         configurable / 身份稳定（两次读取同引用）
const char *kProbeGlobal = R"JS((function () {
  function probe(name) {
    var pre = {
      in: (name in globalThis),
      keys: (Object.keys(globalThis).indexOf(name) >= 0),
      own: (Object.getOwnPropertyNames(globalThis).indexOf(name) >= 0),
      acc: (function () {
        var d = Object.getOwnPropertyDescriptor(globalThis, name);
        return !!(d && typeof d.get === 'function' && !('value' in d));
      })(),
      enumable: (function () {
        var d = Object.getOwnPropertyDescriptor(globalThis, name);
        return !!(d && d.enumerable);
      })(),
      conf: (function () {
        var d = Object.getOwnPropertyDescriptor(globalThis, name);
        return !!(d && d.configurable);
      })()
    };
    var t = typeof globalThis[name];
    var d1 = Object.getOwnPropertyDescriptor(globalThis, name);
    var post = {
      typeof: t,
      data: !!(d1 && ('value' in d1) && !d1.get),
      writable: !!(d1 && d1.writable),
      enumable: !!(d1 && d1.enumerable),
      conf: !!(d1 && d1.configurable),
      identity: (globalThis[name] === globalThis[name])
    };
    return { pre: pre, post: post };
  }
  return JSON.stringify(probe('__N'));
})())JS";

// 宿主对象子属性探测（qwrt.fs / qwrt.storage）：
//   宿主对象 eager 数据属性；子属性 pre 为 accessor、post 为 data property。
const char *kProbeProp = R"JS((function () {
  function probe(hostName, propName) {
    var host = globalThis[hostName];
    var pre = {
      hostData: (function () {
        var d = Object.getOwnPropertyDescriptor(globalThis, hostName);
        return !!(d && ('value' in d) && !d.get && d.writable && d.enumerable && d.configurable);
      })(),
      in: (propName in host),
      keys: (Object.keys(host).indexOf(propName) >= 0),
      own: (Object.getOwnPropertyNames(host).indexOf(propName) >= 0),
      acc: (function () {
        var d = Object.getOwnPropertyDescriptor(host, propName);
        return !!(d && typeof d.get === 'function' && !('value' in d));
      })(),
      enumable: (function () {
        var d = Object.getOwnPropertyDescriptor(host, propName);
        return !!(d && d.enumerable);
      })(),
      conf: (function () {
        var d = Object.getOwnPropertyDescriptor(host, propName);
        return !!(d && d.configurable);
      })()
    };
    var v = host[propName];
    var d1 = Object.getOwnPropertyDescriptor(host, propName);
    var post = {
      typeof: typeof v,
      data: !!(d1 && ('value' in d1) && !d1.get),
      writable: !!(d1 && d1.writable),
      enumable: !!(d1 && d1.enumerable),
      conf: !!(d1 && d1.configurable),
      identity: (host[propName] === host[propName])
    };
    return { pre: pre, post: post };
  }
  return JSON.stringify(probe('qwrt', '__P'));
})())JS";

// 表面等价期望表：每个 lazy API 的 eager 基线 descriptor/typeof。
// 与 lazy 构建后逐项比对（差异必须为空）。writable/enumerable/configurable 与
// 各 setup 原样赋值一致（全部为 true；localStorage 等 P1 特殊项另行覆盖）。
const char *kSurfaceTable = R"JS((function () {
  var expect = {
    BroadcastChannel: { writable: true, enumerable: true, configurable: true, typeof: 'function' },
    URLPattern:       { writable: true, enumerable: true, configurable: true, typeof: 'function' },
    Cache:            { writable: true, enumerable: true, configurable: true, typeof: 'function' },
    CacheStorage:     { writable: true, enumerable: true, configurable: true, typeof: 'function' },
    caches:           { writable: true, enumerable: true, configurable: true, typeof: 'object' }
  };
  var diffs = [];
  var names = Object.keys(expect);
  for (var i = 0; i < names.length; i++) {
    var n = names[i];
    void globalThis[n];            /* 首次访问触发 setup */
    var d = Object.getOwnPropertyDescriptor(globalThis, n);
    if (!d || ('value' in d) === false || d.get) { diffs.push(n + ':not-data'); continue; }
    if (d.writable !== expect[n].writable) diffs.push(n + ':writable');
    if (d.enumerable !== expect[n].enumerable) diffs.push(n + ':enumerable');
    if (d.configurable !== expect[n].configurable) diffs.push(n + ':configurable');
    if (typeof globalThis[n] !== expect[n].typeof) diffs.push(n + ':typeof=' + typeof globalThis[n]);
  }
  return JSON.stringify({ diffs: diffs });
})())JS";

// 删除语义：delete 后消失且不复活。
const char *kDeleteSemantics = R"JS((function () {
  function del(name) {
    void globalThis[name];          /* materialize */
    delete globalThis[name];
    var gone = !(name in globalThis);
    var v1 = globalThis[name];
    var v2 = globalThis[name];
    var noResurrect = (v1 === undefined) && (v2 === undefined);
    return { gone: gone, noResurrect: noResurrect };
  }
  return JSON.stringify({ URLPattern: del('URLPattern'), caches: del('caches') });
})())JS";

// 子属性删除语义：delete qwrt.fs 后消失且不复活。
const char *kDeletePropSemantics = R"JS((function () {
  void globalThis.qwrt.fs;
  delete globalThis.qwrt.fs;
  var gone = !('fs' in globalThis.qwrt);
  var v1 = globalThis.qwrt.fs;
  var v2 = globalThis.qwrt.fs;
  return JSON.stringify({ gone: gone, noResurrect: (v1 === undefined) && (v2 === undefined) });
})())JS";

std::string runProbe(HostCtx *h, const char *name) {
    std::string code = std::string(kProbeGlobal);
    std::string::size_type pos;
    while ((pos = code.find("__N")) != std::string::npos) code.replace(pos, 3, name);
    std::string out;
    EXPECT_TRUE(host_value(h, code.c_str(), &out));
    return out;
}

std::string runProbeProp(HostCtx *h, const char *prop) {
    std::string code = std::string(kProbeProp);
    std::string::size_type pos;
    while ((pos = code.find("__P")) != std::string::npos) code.replace(pos, 3, prop);
    std::string out;
    EXPECT_TRUE(host_value(h, code.c_str(), &out));
    return out;
}

bool has(const std::string &s, const char *sub) {
    return s.find(sub) != std::string::npos;
}

// 断言一个探测 JSON 满足：pre = 可见性/accessor（in/keys/own/enumerable/configurable）
// 与 post = 数据属性（writable/enumerable/configurable/identity）。typeof 期望由调用方给出。
void expectContract(const std::string &v, const char *typeofName,
                    bool isGlobal = true) {
    EXPECT_TRUE(has(v, "\"in\":true"));
    EXPECT_TRUE(has(v, "\"keys\":true"));
    EXPECT_TRUE(has(v, "\"own\":true"));
    EXPECT_TRUE(has(v, "\"acc\":true"));
    EXPECT_TRUE(has(v, "\"enumable\":true"));
    EXPECT_TRUE(has(v, "\"conf\":true"));
    EXPECT_TRUE(has(v, "\"typeof\":"));
    EXPECT_NE(std::string::npos, v.find(typeofName));
    EXPECT_TRUE(has(v, "\"data\":true"));
    EXPECT_TRUE(has(v, "\"writable\":true"));
    EXPECT_TRUE(has(v, "\"identity\":true"));
    (void)isGlobal;
}

} // namespace

// 1. 全局名：URLPattern —— 首次访问前 in/keys/own 可见 + accessor；首次访问后
//    数据属性、descriptor 逐位一致、身份稳定。
TEST_F(PolyfillLazyTest, GlobalNameContractURLPattern) {
    std::string v = runProbe(h, "URLPattern");
    expectContract(v, "\"typeof\":\"function\"");
}

// 2. 全局名：BroadcastChannel
TEST_F(PolyfillLazyTest, GlobalNameContractBroadcastChannel) {
    std::string v = runProbe(h, "BroadcastChannel");
    expectContract(v, "\"typeof\":\"function\"");
}

// 3. 全局名：CacheStorage
TEST_F(PolyfillLazyTest, GlobalNameContractCacheStorage) {
    std::string v = runProbe(h, "CacheStorage");
    expectContract(v, "\"typeof\":\"function\"");
}

// 4. 全局名：caches（实例对象，typeof=object）
TEST_F(PolyfillLazyTest, GlobalNameContractCaches) {
    std::string v = runProbe(h, "caches");
    expectContract(v, "\"typeof\":\"object\"");
}

// 5. 宿主对象子属性：qwrt.fs（宿主 qwrt eager 空壳；子属性 accessor→data）
TEST_F(PolyfillLazyTest, HostPropContractFs) {
    std::string v = runProbeProp(h, "fs");
    EXPECT_TRUE(has(v, "\"hostData\":true"));
    EXPECT_TRUE(has(v, "\"in\":true"));
    EXPECT_TRUE(has(v, "\"keys\":true"));
    EXPECT_TRUE(has(v, "\"own\":true"));
    EXPECT_TRUE(has(v, "\"acc\":true"));
    EXPECT_TRUE(has(v, "\"enumable\":true"));
    EXPECT_TRUE(has(v, "\"conf\":true"));
    EXPECT_NE(std::string::npos, v.find("\"typeof\":\"object\""));
    EXPECT_TRUE(has(v, "\"data\":true"));
    EXPECT_TRUE(has(v, "\"writable\":true"));
    EXPECT_TRUE(has(v, "\"identity\":true"));
}

// 6. 宿主对象子属性：qwrt.storage
TEST_F(PolyfillLazyTest, HostPropContractStorage) {
    std::string v = runProbeProp(h, "storage");
    EXPECT_TRUE(has(v, "\"hostData\":true"));
    EXPECT_TRUE(has(v, "\"in\":true"));
    EXPECT_TRUE(has(v, "\"keys\":true"));
    EXPECT_TRUE(has(v, "\"own\":true"));
    EXPECT_TRUE(has(v, "\"acc\":true"));
    EXPECT_TRUE(has(v, "\"enumable\":true"));
    EXPECT_TRUE(has(v, "\"conf\":true"));
    EXPECT_NE(std::string::npos, v.find("\"typeof\":\"object\""));
    EXPECT_TRUE(has(v, "\"data\":true"));
    EXPECT_TRUE(has(v, "\"writable\":true"));
    EXPECT_TRUE(has(v, "\"identity\":true"));
}

// 7. 表面等价：期望 descriptor/typeof 表 vs lazy 实际值 —— 差异必须为空。
TEST_F(PolyfillLazyTest, SurfaceEquivalence) {
    std::string out;
    ASSERT_TRUE(host_value(h, kSurfaceTable, &out));
    EXPECT_NE(std::string::npos, out.find("\"diffs\":[]"));
}

// 8. 惰性隔离：仅访问 URLPattern，不得提前 materialize 其他独立单元
//     （BroadcastChannel / caches / qwrt.fs / qwrt.storage）——它们仍应是
//     accessor。证明 lazy 机制确实懒，且一个单元 setup 不污染另一单元。
TEST_F(PolyfillLazyTest, LazyIsolation) {
    std::string out;
    ASSERT_TRUE(host_value(h,
        "void globalThis.URLPattern;                        /* 只触发 UP 单元 */\n"
        "function isAcc(o,p){var d=Object.getOwnPropertyDescriptor(o,p);return !!(d&&typeof d.get==='function'&&!('value' in d));}\n"
        "JSON.stringify({"
        "upData:(function(){var d=Object.getOwnPropertyDescriptor(globalThis,'URLPattern');return !!(d&&('value' in d)&&!d.get);})(),"
        "bcAcc:isAcc(globalThis,'BroadcastChannel'),"
        "cachesAcc:isAcc(globalThis,'caches'),"
        "fsAcc:isAcc(globalThis.qwrt,'fs'),"
        "stAcc:isAcc(globalThis.qwrt,'storage')"
        "})", &out));
    EXPECT_NE(std::string::npos, out.find("\"upData\":true"));
    EXPECT_NE(std::string::npos, out.find("\"bcAcc\":true"));
    EXPECT_NE(std::string::npos, out.find("\"cachesAcc\":true"));
    EXPECT_NE(std::string::npos, out.find("\"fsAcc\":true"));
    EXPECT_NE(std::string::npos, out.find("\"stAcc\":true"));
}

// 9. 删除语义：materialize 后 delete → 消失且不复活（全局名）。
TEST_F(PolyfillLazyTest, DeleteSemantics) {
    std::string out;
    ASSERT_TRUE(host_value(h, kDeleteSemantics, &out));
    EXPECT_NE(std::string::npos, out.find("\"gone\":true"));
    EXPECT_NE(std::string::npos, out.find("\"noResurrect\":true"));
}

// 10. 删除语义：子属性 delete qwrt.fs → 消失且不复活。
TEST_F(PolyfillLazyTest, DeletePropSemantics) {
    std::string out;
    ASSERT_TRUE(host_value(h, kDeletePropSemantics, &out));
    EXPECT_NE(std::string::npos, out.find("\"gone\":true"));
    EXPECT_NE(std::string::npos, out.find("\"noResurrect\":true"));
}

// 11. 重复访问不触发重复 setup：两次读取同一引用（身份稳定已在 probe 断言），
//     且首次访问后 descriptor 保持数据属性不退化。
TEST_F(PolyfillLazyTest, RepeatedAccessStable) {
    std::string out;
    ASSERT_TRUE(host_value(h,
        "var a = globalThis.URLPattern;\n"
        "var b = globalThis.URLPattern;\n"
        "var d = Object.getOwnPropertyDescriptor(globalThis, 'URLPattern');\n"
        "JSON.stringify({same: (a === b), data: !!(d && ('value' in d) && !d.get)})", &out));
    EXPECT_NE(std::string::npos, out.find("\"same\":true"));
    EXPECT_NE(std::string::npos, out.find("\"data\":true"));
}

// 级联断言 1（§2.5 bullet 3）：navigator.serviceWorker 首次访问 →
// Worker(W)/MessageChannel(M) 级联 materialize，各自退化为数据属性且身份
// 稳定——子属性 getter 触发的级联不泄漏中间态。
const char *kCascadeSW = R"JS((function () {
  var preSW = Object.getOwnPropertyDescriptor(globalThis.navigator, 'serviceWorker');
  var preAcc = !!(preSW && typeof preSW.get === 'function' && !('value' in preSW));
  var sw1 = globalThis.navigator.serviceWorker;
  var dW = Object.getOwnPropertyDescriptor(globalThis, 'Worker');
  var dM = Object.getOwnPropertyDescriptor(globalThis, 'MessageChannel');
  var dSW = Object.getOwnPropertyDescriptor(globalThis.navigator, 'serviceWorker');
  var sw2 = globalThis.navigator.serviceWorker;
  return JSON.stringify({
    preAcc: preAcc,
    wData: !!(dW && 'value' in dW && !dW.get),
    mData: !!(dM && 'value' in dM && !dM.get),
    swData: !!(dSW && 'value' in dSW && !dSW.get),
    swType: typeof sw1,
    stable: (sw1 === sw2),
    wType: typeof globalThis.Worker,
    mType: typeof globalThis.MessageChannel
  });
})())JS";

// 级联断言 2：WebSocket 首次访问 → CS（CryptoKey/SubtleCrypto/crypto.subtle）
// materialize——握手 SHA-1 依赖 crypto.subtle，级联必须先于 WS 面就绪。
// setupWebSocket 带运行时能力门控（pal.tcpConnect）：mock-libuv 测试环境无
// 网络时 setup 空转早退、WS 面与 eager 语义一致地缺席（首访后属性消失）。
// 故本测试只断言"访问 WS 必先物化 CS"的级联副作用，不断言 WS 本体；subtle
// 类型同样不断言：CRYPTO_EXT ON 为 SubtleCrypto 实例、OFF 为 undefined。
const char *kCascadeWS = R"JS((function () {
  var preSub = Object.getOwnPropertyDescriptor(globalThis.crypto, 'subtle');
  var preAcc = !!(preSub && typeof preSub.get === 'function' && !('value' in preSub));
  var ws1 = globalThis.WebSocket;
  var dWS = Object.getOwnPropertyDescriptor(globalThis, 'WebSocket');
  var dCK = Object.getOwnPropertyDescriptor(globalThis, 'CryptoKey');
  var dST = Object.getOwnPropertyDescriptor(globalThis, 'SubtleCrypto');
  var dSub = Object.getOwnPropertyDescriptor(globalThis.crypto, 'subtle');
  return JSON.stringify({
    preAcc: preAcc,
    wsData: !!(dWS && 'value' in dWS && !dWS.get),
    ckData: !!(dCK && 'value' in dCK && !dCK.get),
    stData: !!(dST && 'value' in dST && !dST.get),
    subData: !!(dSub && 'value' in dSub && !dSub.get),
    wsType: typeof globalThis.WebSocket,
    subtleIn: ('subtle' in globalThis.crypto),
    stable: (globalThis.WebSocket === ws1)
  });
})())JS";

// GRPC OFF 门控面（QWRT_WITH_GRPC=OFF 构建，本测试随 OFF 变体 ctest 跑）：
// lazy 注册式 G 单元在 stub 导入（空函数）下不产生任何面——grpc/protobuf
// 全局名缺席、qwrt.http2 缺席（qwrt 空壳 keys 无 http2）；qwrt 宿主与 fs
// 子属性 accessor 不受影响。
const char *kGrpcOffAbsent = R"JS((function () {
  var q = globalThis.qwrt;
  return JSON.stringify({
    grpcAbsent: !('grpc' in globalThis) && !('protobuf' in globalThis),
    http2Absent: !q || (!('http2' in q) && Object.keys(q).indexOf('http2') < 0),
    qwrtHost: (typeof q === 'object' && q !== null),
    fsIn: !!q && 'fs' in q
  });
})())JS";

// 12. 级联：navigator.serviceWorker → Worker/MessageChannel（子属性 getter
//     触发的 W→M 级联后三面全部为数据属性、身份稳定）。
TEST_F(PolyfillLazyTest, CascadeServiceWorker) {
    std::string out;
    ASSERT_TRUE(host_value(h, kCascadeSW, &out));
    EXPECT_TRUE(has(out, "\"preAcc\":true")) << out;
    EXPECT_TRUE(has(out, "\"wData\":true")) << out;
    EXPECT_TRUE(has(out, "\"mData\":true")) << out;
    EXPECT_TRUE(has(out, "\"swData\":true")) << out;
    EXPECT_TRUE(has(out, "\"stable\":true")) << out;
    EXPECT_TRUE(has(out, "\"wType\":\"function\"")) << out;
    EXPECT_TRUE(has(out, "\"mType\":\"function\"")) << out;
}

// 13. 级联：访问 WebSocket → CS 单元 ensure 先行执行——crypto.subtle 从
//     accessor 退化为数据属性且身份稳定、'subtle' in crypto 恒真。断言与
//     CRYPTO_EXT 无关：ON 时 subtle=SubtleCrypto 实例、CryptoKey/SubtleCrypto
//     类存在；OFF 时 subtle=undefined、类缺席（与 eager 语义一致）——两类
//     环境下 subData/subtleIn/stable/preAcc 均成立。mock-libuv（无
//     pal.tcpConnect）下 WS 本体缺席，不断言（真网络构建见 e2e）。
TEST_F(PolyfillLazyTest, CascadeWebSocketCryptoSubtle) {
    std::string out;
    ASSERT_TRUE(host_value(h, kCascadeWS, &out));
    EXPECT_TRUE(has(out, "\"preAcc\":true")) << out;
    EXPECT_TRUE(has(out, "\"subData\":true")) << out;
    EXPECT_TRUE(has(out, "\"subtleIn\":true")) << out;
    EXPECT_TRUE(has(out, "\"stable\":true")) << out;
}

// 14. GRPC OFF 门控：lazy G 单元零注册（grpc/protobuf/qwrt.http2 全缺席）。
TEST_F(PolyfillLazyTest, GrpcOffSurfaceAbsent) {
    std::string out;
    ASSERT_TRUE(host_value(h, kGrpcOffAbsent, &out));
    EXPECT_TRUE(has(out, "\"grpcAbsent\":true")) << out;
    EXPECT_TRUE(has(out, "\"http2Absent\":true")) << out;
    EXPECT_TRUE(has(out, "\"qwrtHost\":true")) << out;
    EXPECT_TRUE(has(out, "\"fsIn\":true")) << out;
}

// setter 语义（§2.5 / lazy.js）：lazy 期间对 accessor 赋值 → ensure 先物化再
// 落值（monkey-patch 与 eager 一致）；产物为数据属性、身份稳定、覆盖生效。
const char *kSetterProbe = R"JS((function () {
  globalThis.URLPattern = function FakePattern() {};
  var d = Object.getOwnPropertyDescriptor(globalThis, 'URLPattern');
  return JSON.stringify({
    data: !!(d && 'value' in d && !d.get),
    writable: !!(d && d.writable),
    fn: (typeof globalThis.URLPattern === 'function'),
    name: globalThis.URLPattern.name,
    stable: (globalThis.URLPattern === globalThis.URLPattern)
  });
})())JS";
// localStorage 特殊面：materialize 后为 writable:false 数据属性——strict 赋值
// 抛 TypeError（与 eager 一致，见 local-storage.js defineProperty）。
const char *kLocalStorageProbe = R"JS((function () {
  'use strict';
  var ls = globalThis.localStorage;
  var d = Object.getOwnPropertyDescriptor(globalThis, 'localStorage');
  var err = null;
  try { globalThis.localStorage = {}; } catch (e) { err = e.name; }
  return JSON.stringify({
    data: !!(d && 'value' in d && !d.get),
    writable: !!(d && d.writable),
    err: err,
    lsType: typeof ls
  });
})())JS";

// 15. setter 先 ensure 再落值（monkey-patch 与 eager 同语义）。
TEST_F(PolyfillLazyTest, SetterMaterializeThenAssign) {
    std::string out;
    ASSERT_TRUE(host_value(h, kSetterProbe, &out));
    EXPECT_TRUE(has(out, "\"data\":true")) << out;
    EXPECT_TRUE(has(out, "\"writable\":true")) << out;
    EXPECT_TRUE(has(out, "\"fn\":true")) << out;
    EXPECT_TRUE(has(out, "\"name\":\"FakePattern\"")) << out;
    EXPECT_TRUE(has(out, "\"stable\":true")) << out;
}

// 16. localStorage non-writable：materialize 后 strict 赋值抛 TypeError。
TEST_F(PolyfillLazyTest, LocalStorageNonWritable) {
    std::string out;
    ASSERT_TRUE(host_value(h, kLocalStorageProbe, &out));
    EXPECT_TRUE(has(out, "\"data\":true")) << out;
    EXPECT_TRUE(has(out, "\"writable\":false")) << out;
    EXPECT_TRUE(has(out, "\"err\":\"TypeError\"")) << out;
    EXPECT_TRUE(has(out, "\"lsType\":\"object\"")) << out;
}
