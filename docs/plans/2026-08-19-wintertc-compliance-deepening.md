# WinterTC 合规深化 + HTTPServer 优化 Implementation Plan

> **REQUIRED SUB-SKILL:** Use the executing-plans skill to implement this plan task-by-task.

**Goal:** 在 WinterTC（ECMA-429）接口层全部补齐的基础上，深化 fetch/streams 语义完整性、补 Worker MessagePort 多跳转移、扩展 WinterTC 合规 gtest 覆盖，并优化 HTTPServer 性能瓶颈。

**Architecture:** 四阶段推进，每阶段独立可交付，阶段间有回归 checkpoint：
- Phase 1 — fetch/streams 语义深化（纯 polyfill JS 层，TDD + gtest，风险最低）
- Phase 2 — Worker MessagePort 多跳转移（C 层 bridge.c + polyfill message-channel.js，纠缠关系重建，风险最高）
- Phase 3 — WinterTC 合规 gtest 覆盖扩展（test/ 补用例，暴露并修复真实 bug）
- Phase 4 — HTTPServer 性能优化（ext_compress.c / uvhttp，gzip 与大响应吞吐）

**Tech Stack:** C99 + QuickJS-ng + libuv（C 层）；ES module polyfill 经 esbuild 打包（JS 层，`polyfill/build.js`，QJSC 自动探测 + absWorkingDir 已修复）；gtest + mock_libuv（测试，`QWRT_BUILD_TESTS=ON` 构建于 `build/`）；miniz（压缩）。

---

# Phase 1: fetch/streams 语义深化

**TDD scenario:** New feature — full TDD cycle per task。

**现状（调研 2026-08-19）：**
- `polyfill/src/fetch.js`：`pal.httpRequestStream(url, method, headers_json, body, onHeaders, onData, onEnd)`。`Request` 有 `_signal`/`_body`/`_headers`，但**不处理** `redirect`/`keepalive`/`cache`/`mode`/`credentials` 选项（构造时丢弃）。
- `polyfill/src/streams.js`：`tee()` 的分支 cancel 是空注释（`// Release lock when both branches are cancelled`，未实现锁释放/取消传播）；`pipeTo()` 是简化实现（无背压等待、无 abort 传播）。
- `polyfill/src/encoding.js` / `text-encoding.js`：`TextDecoderStream` 跨 chunk 的多字节字符边界未专门测试。

## Task 1.1: Request 构造接收并存储 redirect/keepalive/cache 等选项

**Files:**
- Modify: `polyfill/src/fetch.js`（`Request` 构造函数 + getter 区，约 224-265 行）
- Test: `test/test_polyfill_gtest.cpp`（新增 `FetchRequestOptions` 用例）

**Step 1: 写失败测试**

在 `test/test_polyfill_gtest.cpp` 追加（用 `host_value` 同步求值，因为这些 getter 是同步的）：

```cpp
TEST_F(PolyfillTest, FetchRequestOptions) {
    std::string v;
    ASSERT_TRUE(host_value(h,
        "var r = new Request('https://x.com', {redirect:'error', keepalive:true, cache:'no-store', mode:'cors', credentials:'include'});\n"
        "JSON.stringify([r.redirect, r.keepalive, r.cache, r.mode, r.credentials])", &v));
    EXPECT_NE(std::string::npos, v.find("\"error\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"no-store\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"cors\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"include\"")) << "got: " << v;
}
```

**Step 2: 跑测试确认失败**

```bash
cmake --build build --target test_polyfill_gtest -j$(nproc)
./build/test/test_polyfill_gtest --gtest_filter='PolyfillTest.FetchRequestOptions'
```
预期：FAIL（`r.redirect` 等是 `undefined`，`v.find` 找不到 `"error"`）。

**Step 3: 实现**

在 `polyfill/src/fetch.js` 的 `Request` 构造函数（两个分支：`init` 来自 input 和独立 init）各加字段：

```js
this._redirect = init.redirect || (input && input.redirect) || 'follow';
this._keepalive = !!init.keepalive;
this._cache = init.cache || 'default';
this._mode = init.mode || 'cors';
this._credentials = init.credentials || 'same-origin';
```

并在 getter 区（`headers`/`body`/`bodyUsed`/`signal` getter 附近）加对应 `Object.defineProperty`。

**Step 4: 跑测试确认通过**

预期：PASS。

**Step 5: 提交**

```bash
git add polyfill/src/fetch.js test/test_polyfill_gtest.cpp
git commit -m "feat(fetch): Request stores redirect/keepalive/cache/mode/credentials options"
```

## Task 1.2: fetch redirect 行为

**Files:**
- Modify: `polyfill/src/fetch.js`（`fetch()` 主流程，`pal.httpRequestStream` 调用处）
- Test: `test/test_polyfill_gtest.cpp` 或 `test/test_fetch_stream_gtest.cpp`

**Step 1: 写失败测试** — 用 mock TCP 返回 3xx + Location，断言 `redirect:'error'` 时 fetch reject；`redirect:'manual'` 时返回 opaque/空 body；`redirect:'follow'` 时跟随（mock 需二次响应，若 mock 难做则聚焦 `error` 语义）。

**Step 2-4:** TDD 循环。**Step 3 实现要点**：`fetch()` 里读 `request.redirect`；收到 3xx 状态时按 `redirect` 值分支（`error` → reject `TypeError('redirect')`；`manual` → 返回 status=0 的空 Response；`follow` → 若 Location 存在且非 loop 则重发，否则返回原响应）。注意 C 层 `pal.httpRequestStream` 只做单次请求，follow 需在 JS 层循环调用。

**Step 5: 提交** `feat(fetch): redirect follow/error/manual semantics`

## Task 1.3: ReadableStream.tee() 补 cancel 传播

**Files:**
- Modify: `polyfill/src/streams.js`（`tee()` 方法，约 417-468 行）
- Test: `test/test_polyfill_gtest.cpp`（新增 `StreamTeeCancel` 用例）

**Step 1: 写失败测试**（异步，`host_eval` + `host_poll_until_value`，**变量必须预声明**避免 flaky）：

```cpp
TEST_F(PolyfillTest, StreamTeeCancel) {
    std::string v;
    ASSERT_TRUE(host_eval(h,
        R"(var _tc = null;
        var srcCancelled = false;
        var s = new ReadableStream({
          type: 'bytes',
          start: function(c){ c.enqueue(new Uint8Array([1,2,3])); },
          cancel: function(){ srcCancelled = true; }
        });
        var branches = s.tee();
        branches[0].cancel().then(function(){
          _tc = JSON.stringify([srcCancelled, branches[1].locked]);
        });
        0)", &v));
    /* 分支 0 取消应传播到源 cancel，分支 1 应仍可用 */
    ASSERT_TRUE(host_poll_until_value(h, "_tc", "true", &v));
}
```

**Step 2:** 跑测试确认失败（当前 tee 的 cancel 不调用源 cancel，`srcCancelled` 保持 false）。

**Step 3: 实现** — `tee()` 的 `cancel` 回调改为：源 reader 加跟踪；任一分支取消时，若两分支都已取消（或只有一分支存在），`reader.cancel()` 释放源锁；`branch.cancel` 返回 Promise。用闭包计数两分支取消状态。

**Step 4-5:** 确认 PASS + 提交 `fix(streams): tee() propagates cancel to underlying source`

## Task 1.4: pipeTo 背压与 abort/close 传播

**Files:**
- Modify: `polyfill/src/streams.js`（`pipeTo()` 方法，约 471-490 行）
- Test: `test/test_polyfill_gtest.cpp`（`StreamPipeToAbort` 用例）

**Step 1: 写失败测试** — 读端 errored 时 `pipeTo` 应 reject 且 writer 被 abort；写端背压（writer.write 返回 pending promise）时 pump 不并发推进。

**Step 2-4:** TDD 循环。**Step 3 实现要点**：`pipeTo` 里 `writer.write(result.value).then(pump)` 已有串行，重点补：读 reject 时 `writer.abort(e)` 已存在但需确保 reject 传播；读 done 时 `writer.close()` 失败应 reject；加 `preventAbort`/`preventClose` 参数可选。

**Step 5:** 提交 `fix(streams): pipeTo backpressure + abort/close propagation`

## Task 1.5: TextDecoderStream 跨 chunk 多字节边界

**Files:**
- Test: `test/test_polyfill_gtest.cpp`（新增 `TextDecoderStreamMultibyte` 用例）
- Modify（若暴露 bug）: `polyfill/src/streams.js`（`TextDecoderStream` 类，约 690-725 行）或 `encoding.js`

**Step 1: 写测试** — 把 3 字节 UTF-8 字符（如 `'€'` = `E2 82 AC`）分两个 chunk 写入 `TextDecoderStream` 的 writable，断言 readable 输出拼接后正确。当前实现每次 `write` 调 `decoder.decode(chunk, {stream:true})`，跨 chunk 依赖 TextDecoder 内部残留状态——需验证 `TextDecoder` 是否在流式模式下正确保留半字符。

**Step 2-4:** TDD 循环；若 `TextDecoder.decode(..., {stream:true})` 已正确处理则测试直接 PASS（验证性质）；否则修复。

**Step 5:** 提交 `test(encoding): TextDecoderStream multibyte chunk-boundary coverage`

## Phase 1 结束 checkpoint

```bash
cmake --build build -j$(nproc)
cd build && ctest -L offline --output-on-failure
```
全绿后进入 Phase 2。注意 compress 相关 gtest 有已知 flaky（brain `worker-transferable` 页记录），单独重跑确认。

---

# Phase 2: Worker MessagePort 多跳转移

**TDD scenario:** New feature — full TDD cycle。

**背景（brain `worker-transferable` 页，2026-08-19）：** v1 支持单次跨线程转移（父→worker 或 worker→父）。**多跳（父→worker→父）依赖纠缠关系重建，v1 未支持**。当前 `message-channel.js` 的 port 用 `_id`（全局唯一，`pal.portCreate`）/`_peerId`/`_peerThread`（`'local'|'parent'|workerId>0`）路由；C 层 `bridge.c` 提供 `portCreate`/`workerPost`/`postMessage`。

## Task 2.1: 多跳语义分析 + 设计（无代码）

**Files:** 只读调研，不改代码
- Read: `polyfill/src/message-channel.js`（242 行）
- Read: `src/bridge.c`（`pal.portCreate`/`pal.workerPost`/`pal.workerId` 实现）
- Read: `src/worker.c`（boot shim 的 `__qwrt_dispatch__`/`__qwrt_deliver_port_msg__`）

**产出：** 在本文档下方追加"多跳转移设计"小节，明确：
1. port 在父→worker→父 过程中 `_id`/`_peerId`/`_peerThread` 如何流转
2. worker 侧收到已 transfer 的 port 后再 postMessage 给父时，`_peerThread` 应指向谁
3. C 层是否需要新增（如 port 表跨线程同步，或仅 JS 层改路由）

## Task 2.2: C 层支持（如需）

**Files:**
- Modify: `src/bridge.c` / `src/worker.c`
- Test: `test/test_worker_gtest.cpp`

按 Task 2.1 设计：若需跨线程 port id 映射，在 bridge.c 加全局 port 表（id → ownerThread/对端 workerId），transfer 时更新归属；worker 侧 `pal.workerId` 已存在（v1 worker→父 方向加的）。

## Task 2.3: polyfill 层多跳路由

**Files:**
- Modify: `polyfill/src/message-channel.js`
- Test: `test/test_worker_gtest.cpp`（新增 `transfer_messageport_multihop`）

**Step 1: 写失败测试**（gtest，参考现有 `transfer_messageport_worker_to_parent` 用例写法）：

```cpp
// 父创建 channel，port1 转移给 worker；worker 收到后把 port 再 postMessage 回父；
// 父侧最终收到经多跳回来的 port，能与其 echo 通信。
TEST_F(WorkerTest, transfer_messageport_multihop) { /* ... */ }
```

**Step 2-4:** TDD 循环。**Step 3 实现要点**：worker 侧把收到的 port（`_peerThread` 指向父/workerId）再次 transfer 时，boot shim 的转移 ref 需保留原 port 的纠缠关系（`_peerId` 不变，`_peerThread` 更新为下一跳）；`__qwrt_deliver_port_msg__` 需处理已 transfer 的 port 再转发。

**Step 5:** 提交 `feat(worker): MessagePort multihop transfer (parent→worker→parent)`

### 多跳转移设计（Task 2.1 产出）

**`_peerThread` 语义**：记录「纠缠对端 port 所在线程」（相对当前线程）：`'local'`=对端在当前线程（同线程 `_entangledPort` 纠缠）；`'parent'`=对端在父线程；`workerId>0`=对端在那个 worker 线程。`_peerId` 在整个多跳过程中**不变**（纠缠对端的全局 id）。

**转移 ref 语义**：`{id, peerId, peerThread}` 中 `peerThread` = **被转移 port 的对端所在线程（从接收方线程视角）**。对端所在线程在转移前后不变，因此：
- 对端在当前线程（`p._peerThread === 'local'`）→ 对端留在本线程 → ref.peerThread = 本线程标签（父='parent'；worker=workerId）。
- 对端已在别的线程（`p._peerThread` 是 `'parent'`/workerId）→ 保持不变，ref.peerThread = `p._peerThread`。

**多跳数据流（父→worker→父）**：
1. 父 `new MessageChannel()` → id1/id2，port1 对端=port2（父侧本地纠缠，`_peerThread='local'`）。
2. 父 `w.postMessage(data, [port1])`：ref={id:id1, peerId:id2, peerThread:'parent'}（对端 port2 在父）；port1._detached=true；父侧 port2._peerThread ← workerId。
3. worker boot shim 收到 → `__qwrt_port_from_ref__` 建 port1'（id1, peerId=id2），本地 lookupPort(id2) 无 → `_peerThread='parent'`（对端 port2 在父）。worker 经 port1' 与父侧 port2 通信已通（v1）。
4. worker 再 `postMessage(data, [port1'])` 把 port1' 转回父：**bug**——boot shim 硬编码 `peerThread: pal.workerId()`，使父侧认为对端在 worker（实际对端 port2 在父）。
5. 父侧收到 ref={id:id1, peerId:id2, peerThread:<值>} → `__qwrt_port_from_ref__` 建 port1''。父侧本地已有 port2（lookupPort(id2) 命中）→ **重建同线程纠缠**：port1''._peerThread='local'、port1''._entangledPort=port2；port2._peerThread='local'、port2._entangledPort=port1''。父侧 port1''↔port2 即可同线程 echo。

**修改点（均为 JS 层，C 层 bridge.c 无需新增——port id 全局唯一、路由信息随 ref 流转，无需跨线程 port 表同步）**：
1. `src/worker.c` boot shim `globalThis.postMessage`：`peerThread: pal.workerId()` → `peerThread: (t._peerThread === 'local' ? pal.workerId() : t._peerThread)`（worker 侧对端只可能在本 worker('local') 或父('parent')）。
2. `polyfill/src/worker.js` 父侧 `Worker.prototype.postMessage`：`peerThread: 'parent'` → `peerThread: (t._peerThread === 'local' ? 'parent' : t._peerThread)`（父侧对端只可能在本线程('local') 或某 worker(workerId)）。
3. `polyfill/src/message-channel.js` `__qwrt_port_from_ref__`：先查本地表对端，若命中则重建同线程纠缠（双方 `_peerThread='local'` + `_entangledPort` 互指）；否则维持 `_peerThread = info.peerThread` 走远程路由。

**复用验证**：单跳路径不受影响（父→worker：worker 侧 lookupPort(peerId) 无 → 远程，ref.peerThread='parent' 不变；worker→父：父侧 lookupPort(peerId) 无 → 远程，ref.peerThread=workerId 不变）。多 worker 并发、terminate 后代理等边界亦不回归。

## Phase 2 结束 checkpoint

```bash
cmake --build build -j$(nproc)
cd build && ./test/test_worker_gtest && ctest -L offline --output-on-failure
```
worker 套件原有 12 用例必须全过 + 新增 multihop 用例。

---

# Phase 3: WinterTC 合规 gtest 覆盖扩展

**TDD scenario:** Modifying tested code — run existing tests first。

**背景（brain `wpt-runner-removed` 页）：** WPT runner 已移除，WinterTC 覆盖转向 gtest。`test/wpt/` 是遗留目录（不再运行）。目标：给已实现但 gtest 覆盖不足的模块补用例，暴露真实 bug。

## Task 3.1: 覆盖审计

**Files:** 只读
- 对照 `polyfill/src/` 每个模块（encoding/text-encoding/url/event-target/streams/abort）与 `test/test_*.cpp` 现有用例，列出"已实现但零/弱 gtest 覆盖"的模块。
- 产出覆盖差距表，追加到本计划末尾。

## Task 3.2: encoding gtest（TextDecoder 多字节/fatal/流式）

**Files:**
- Test: `test/test_polyfill_gtest.cpp`（`TextDecoderEdgeCases`）
- Modify（若暴露 bug）: `polyfill/src/text-encoding.js` / `encoding.js`

用例：`new TextDecoder('utf-8', {fatal:true})` 遇非法字节 reject/throw；`decode` 流式半字符；`ignoreBOM`。

## Task 3.3: URL gtest（URLSearchParams 遍历/编码）

**Files:**
- Test: `test/test_polyfill_gtest.cpp`（`UrlSearchParamsEdgeCases`）
- Modify（若暴露 bug）: `polyfill/src/url.js`

用例：`encodeURIComponent` 边界、`URLSearchParams` 特殊字符编码、`sort()`、迭代器。

## Task 3.4: streams gtest（tee/pipeTo/BYOB 更多边界）

**Files:**
- Test: `test/test_polyfill_gtest.cpp`（`StreamEdgeCases`）

用例：`tee()` 双分支独立消费；BYOB reader 对 non-byte stream 抛错；`cancel()` 多次幂等；`ReadableStream` locked 时 `getReader` 抛错。

## Task 3.5: EventTarget gtest（once/abort signal/计数）

**Files:**
- Test: `test/test_polyfill_gtest.cpp`（`EventTargetEdgeCases`）
- Modify（若暴露 bug）: `polyfill/src/event-target.js` / `abort.js`

用例：`{once:true}` 触发一次后移除；`AbortSignal` 触发后 `addEventListener` 立即回调；监听器移除/计数。

## Phase 3 结束 checkpoint

```bash
cd build && ctest -L offline --output-on-failure
```

---

# Phase 4: HTTPServer 性能优化

**TDD scenario:** Modifying tested code — run existing tests first（性能，无断言，用基准对比验证）。

**背景（brain `httpserver-perf-benchmark` 页）：** 瓶颈：① gzip 单线程压缩（14.6k rps vs 非压缩 32k）；② 大响应（100KB）单连接串行吞吐（memcpy + 单核饱和）。

## Task 4.1: 基线复测

**Files:** 无代码改动

```bash
# 复用 /tmp/qwrt_bench.js（plain 18800）、wrk
wrk -t4 -c100 -d10s http://localhost:18800/hello
wrk -t4 -c100 -d10s http://localhost:18800/gzip
wrk -t4 -c100 -d10s http://localhost:18800/big
```
记录基线 rps/p99 到 `/tmp/qwrt_bench_results.md`。

## Task 4.2: gzip 优化

**Files:**
- Modify: `src/ext_compress.c`（miniz 压缩参数，约 30-80 行）
- Modify: `src/ext_http_server.c`（若加静态文件 gzip 预压缩缓存）

方案（按收益排序）：
1. 静态文件 gzip **预生成缓存**：首次压缩后缓存 bytecode，后续请求直接复用（静态路径命中率高）。
2. miniz 压缩级别调优（`tdefl_compress` level 6→9 或用 `tdefl_create_comp_flags` 减少查表）。
3. 动态 gzip 路径避免重复压缩同内容（按内容哈希缓存，LRU）。

## Task 4.3: 大响应吞吐

**Files:**
- Modify: `src/ext_http_server.c` / `deps/uvhttp`（若需）
- Modify: `src/uv_io.c`（memcpy 消除，若大响应走零拷贝路径）

方案：大响应用 `uv_write` 直接引用用户 buffer 避免拷贝；或分块发送降低单次分配。

## Task 4.4: 复测 + 记录

```bash
wrk -t4 -c100 -d10s http://localhost:18800/gzip
wrk -t4 -c100 -d10s http://localhost:18800/big
```
对比 Task 4.1 基线，更新 `/tmp/qwrt_bench_results.md`，用 brain 记录 `httpserver-perf-benchmark` 页更新。

## Phase 4 结束 checkpoint

全套回归（offline ctest）+ httpserver e2e（`QWRT_BUILD_HTTPSERVER_TESTS=ON`，real-libuv 构建）+ 基准对比。

---

# 执行顺序与依赖

```
Phase 1 (fetch/streams JS 层) ──┐
Phase 3 (gtest 覆盖) ────────────┤──> 全部完成后全量回归
Phase 2 (Worker 多跳, C 层) ─────┤
Phase 4 (HTTPServer 性能) ───────┘
```
Phase 1 与 Phase 3 都改 `test/test_polyfill_gtest.cpp`，建议先后执行避免冲突；Phase 2 涉及 C 层 + worker，独立；Phase 4 独立。各 phase 可独立提交、独立验证。

## 关键文件索引

| 文件 | 用途 |
|---|---|
| `polyfill/src/fetch.js` | fetch/Request/Response（Phase 1） |
| `polyfill/src/streams.js` | streams/tee/pipeTo/TextDecoderStream（Phase 1） |
| `polyfill/src/message-channel.js` | MessagePort 路由（Phase 2） |
| `src/bridge.c` | pal.portCreate/workerPost/workerId（Phase 2） |
| `src/worker.c` | boot shim/__qwrt_dispatch__（Phase 2） |
| `test/test_polyfill_gtest.cpp` | polyfill gtest（Phase 1/3） |
| `test/test_worker_gtest.cpp` | worker gtest（Phase 2） |
| `src/ext_compress.c` | miniz 压缩（Phase 4） |
| `src/ext_http_server.c` | HTTP 响应路径（Phase 4） |

## 构建/测试备忘

- polyfill 改动后**必须**重打包：`env -u QJSC node polyfill/build.js`（QJSC 自动探测 + absWorkingDir 保证产物可复现，见工程改善 commit 3d0c2ba9）。
- 测试构建：`cmake --build build -j$(nproc)`；运行：`cd build && ./test/test_<name>_gtest` 或 `ctest -L offline`。
- 产物 `dist/polyfill.{js,bytecode}` + `src/polyfill_default.c` 需 `git add -f`（.gitignore 忽略 dist/，但产物刻意跟踪）。
- 提交时若遇 `index.lock`（gitstatusd 竞态），用 `rm -f .git/index.lock` 后立即重试，或复用 /tmp 下的重试脚本模式。
- compress gtest 有已知 flaky（brain 记录），失败时单独重跑。
