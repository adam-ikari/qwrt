# WPT 集成设计

**日期**: 2026-07-14
**状态**: 设计稿，spike 已验证

## Context

qwrt 是 WinterCG-compatible 的 QuickJS 运行时，但缺少上游规范的合规测试。现有 `test_compliance_gtest` 是手写的 ~50 个测试，不能穷尽、易遗漏边界。

Web Platform Tests (WPT) 是 Web 规范的官方测试集，覆盖 URL/fetch/crypto/streams/EventTarget 等所有 Web API。但 WPT 默认为浏览器设计（依赖 DOM/worker）。

**关键发现（spike 验证）**：WPT 的 `testharness.js`（5207 行）已内置 `ShellTestEnvironment`——为非 DOM 运行时设计的 fallback。环境检测：无 `document` → 无 Worker scopes → 无 ShadowRealm → ShellTestEnvironment。QuickJS 无 `document`，自动走 Shell 路径，**不需要 patch 掉 DOM/worker 依赖**。`.any.js` 测试用纯 `test()`/`assert_*` API，无 DOM。结果通过 completion callback（sync，对同步 test）+ 自定义 report 输出。

Spike 验证（2026-07-14）：testharness.js + 注入的 `print` + shell report + 真实 URL `.any.js` 测试在 qwrt（mock PAL）下完整运行，PASS/FAIL 结果正确捕获。

## 设计

### 1. WPT 子集 vendoring

直接 vendor 选中的 WPT 文件到 `deps/wpt/`（不用 submodule——WPT 仓库数百 MB，submodule 即使 sparse 也重；直接拷贝选中的 `.any.js` + `resources/testharness.js` 更轻、更可控）。覆盖 WinterCG Minimal Common API：
- URL / URLSearchParams (`url/`)
- TextEncoder / TextDecoder (`encoding/`)
- AbortController / AbortSignal (`dom/abort/` 的 `.any.js`，非 `.html`)
- structuredClone (`html/webidl/structured-clone/` 或 `dom/`)
- EventTarget / Event (`dom/events/` 的 `.any.js`)
- MessageChannel / MessagePort (`html/webappapis/messaging/`)
- Blob / File / FormData (`fileapi/`, `xhr/`)
- crypto.subtle (`WebCryptoAPI/`)

不 vendor：`.html`/`.window.js`/`.worker.js`（需 DOM/worker）、非 WinterCG API（CSS/DOM 渲染等）。

来源：从 web-platform-tests/wpt 仓库（master）下载选中的 `.any.js` + `resources/testharness.js`，提交到 `deps/wpt/`。一个 `LICENSE` 文件一起提交（WPT 是 BSD/三许可）。

### 2. Runner：`test/wpt_runner.c`

C 程序，用 qwrt（mock PAL）逐个跑 `.any.js`：
1. 创建 qwrt runtime（mock PAL，`config.host_data` 存 runner state）
2. 注入 `print` 全局（写到 per-runtime buffer）
3. eval `testharness.js`（harness）
4. eval `test/wpt_shell_report.js`（自定义 shell report，注册 completion callback → print 结构化结果）
5. eval 测试文件（`.any.js`）
6. `qwrt_tick`（drain async，对 sync test 是 no-op）
7. 读取 `print` buffer，解析 `PASS|name|msg` / `FAIL|name|msg` / `SUMMARY:N:status`
8. 汇总输出（passed/failed/skipped），exit code 反映失败数

遍历测试目录（命令行参数指定路径），每个文件独立 runtime（隔离，避免 test 间状态泄漏）。

### 3. Shell report：`test/wpt_shell_report.js`

注入 qwrt 的自定义 report：
```js
add_completion_callback(function(tests, harness_status, asserts) {
  var lines = [];
  for (var i = 0; i < tests.length; i++) {
    var t = tests[i];
    var st = t.status === 0 ? "PASS" : (t.status === 1 ? "FAIL" : "TIMEOUT/ERROR");
    lines.push(st + " | " + t.name + " | " + (t.message || ""));
  }
  lines.push("SUMMARY:" + tests.length + ":" + harness_status.status);
  print(lines.join("\n"));
});
```

### 4. 测试过滤

部分 WPT 测试用 qwrt 未实现的 API（如 `fetch` 需 mock HTTP 响应、某些依赖 `MessageChannel` 精确语义）。Runner 按结果分类：
- PASS — 通过
- FAIL — 失败（可能 API 差异，需 triage）
- ERROR/exception — harness 加载失败或 API 缺失（归类为 skip）

不追求 100% pass——WPT 是上游规范，qwrt 的 WinterCG 子集会有已知差异。目标是**发现回归**（之前 pass 的变 fail）和**暴露未覆盖路径**。

### 5. CI 集成

CMake target `wpt_runner`，ctest label `wpt`，`continue-on-error: true`（类似 network job）。CI job 跑 `ctest -L wpt`，结果非 blocking 但暴露回归。

## 关键文件
- 新增：`deps/wpt/`（vendor 的 `.any.js` + `testharness.js` + LICENSE）、`test/wpt_runner.c`、`test/wpt_shell_report.js`
- 改：`test/CMakeLists.txt`（wpt target + label）

## 验证
- spike 已验证 testharness.js 在 qwrt 跑通
- runner 跑 URL/TextEncoder 等子集，PASS/FAIL 正确
- CI wpt job 非 blocking

## 不做
- 不 patch testharness.js（Shell 路径已无 DOM）
- 不跑 `.html`/`.worker.js`
- 不追求 100% WPT pass
