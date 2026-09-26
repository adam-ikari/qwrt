---
title: 调试
description: 使用 DAP 调试器调试 qzjs — 断点、步进、变量检查以及 VS Code 集成。
---

# 使用 VS Code 调试 qzjs 程序

qzjs 内置了一个 **DAP（调试适配器协议）** 步进调试器，直接编译在库本身中——无需单独的调试器二进制文件。启用后，任何嵌入 qzjs 的程序都可以在 VS Code 中进行步进调试（断点、逐过程/步入/步出、调用栈、局部变量、求值）。

## 工作原理

调试器是一种**库能力**，而非独立进程。它位于 `src/debugger.c`（调试核心）和 `src/debugger_dap.c`（DAP 协议层）中，当 `QZ_BUILD_DEBUGGER=ON` 时编译进 `libqzjs.a`。对 QuickJS-ng 引擎的一个小补丁（`deps/quickjs-ng-debugger.patch`）添加了调试核心使用的断点/步进内省原语。

激活方式为**通过配置或环境变量自动**——你的宿主代码无需更改。`qz_create` 检查调试设置，如果启用，则附加 DAP 层（通过 stdin/stdout 进行 DAP 通信）并在入口处暂停。VS Code 随后附加。

### 双层禁用（关闭时零开销）

- `QZ_BUILD_DEBUGGER=OFF`（默认）：**不**应用引擎补丁，**不**编译 `src/debugger.c`/`src/debugger_dap.c`，`qz_create` 中没有调试代码路径。调试功能不存在；`libqzjs.a` 保持不变。
- `QZ_BUILD_DEBUGGER=ON`：应用补丁并编译源文件，但引擎的每个操作码的 `DEBUGGER_CHECK` 是无操作的（一个永不执行的分支），**除非运行时附加了调试器**。非调试运行几乎没有性能开销。

## 构建

```bash
cmake -B build -DQZ_BUILD_DEBUGGER=ON -DQZ_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

这会在配置时将 `deps/quickjs-ng-debugger.patch` 应用到 QuickJS-ng 子模块的工作树（子模块在 git 中保持干净——补丁是事实来源）。`cmake -DQZ_BUILD_DEBUGGER=OFF` 恢复原始状态。

## 在你的程序中启用调试

**方案 A — 无需代码更改（环境变量）：** 使用 `QZ_DEBUG=1` 运行你的程序：

```bash
QZ_DEBUG=1 ./myapp app.js
```

**方案 B — 配置位：** 设置 `qz_config_t.debug` 的位 1（位 0 是现有的详细日志标志）：

```c
qz_config_t cfg = {};
cfg.debug = 0x2;            /* 位 1 = 启用调试（或直接以 QZ_DEBUG=1 运行） */
qz_t *rt = qz_create(&cfg);
qz_eval(rt, src, NULL);   /* 在入口处暂停，然后在断点处暂停 */
```

就这样——`qz_create` 自动附加 DAP，发送 `initialized`，并在 DAP 配置阶段（initialize / setBreakpoints / configurationDone）阻塞后返回。`stop_on_entry` 在程序的第一条语句处暂停。

## 从 VS Code 调试

调试器通过 **stdio 上的标准 DAP** 通信：运行时（启用调试的 `qz_create`）
是 stdin/stdout 上的 DAP 服务端，任何会讲 DAP 的客户端都能连。当前
**没有 VS Code 扩展**注册 `qzjs` 调试类型，所以常见的 `launch.json`
`type: "qzjs"` 配置无法直接使用——VS Code 会报调试适配器类型未注册。
（DAP 层本身已实现且经 `test/test_dap_gtest.cpp` 与任意通用 DAP 客户端
端到端测试过。）

在扩展发布之前，从 VS Code 驱动有两种方式：

**方案 1 —— 通用调试适配器。** 用一个 stdio DAP 适配器（如 Mock Debug
适配器，或自己写的）配一个 launch：`program` 指向在 `QZ_DEBUG=1` 下
运行你二进制的命令。适配器在 VS Code 与子进程 stdio 之间转发 DAP。
任何「DAP 服务端在 stdio、客户端侧」的适配器都能工作；qzjs 侧无需
扩展，因为它从不注册 VS Code 类型。

**方案 2 —— 直接驱动 DAP 协议。** 在 `QZ_DEBUG=1` 下运行你的程序，
自行往 stdin/stdout 发 DAP——可以是 REPL、脚本或一次性客户端。
`test/test_dap_gtest.cpp` 是可用的参考客户端：它在 `QZ_DEBUG=1` 下
fork 子进程，然后发 initialize → setBreakpoints → configurationDone，
期待断点处的 `stopped` 事件，再单步并检查变量。

DAP 层实现了：initialize、attach、setBreakpoints、configurationDone、
threads、stackTrace、scopes、variables、continue、next、stepIn、stepOut、
evaluate、disconnect。

<details>
<summary>参考 `launch.json`（需尚未发布的扩展）</summary>

下面的配置**只有在扩展注册了 `qzjs` 调试类型之后才能用**。这里给出
是作为预期的最终形态，而非当前可跑的设置：

```json
{
  "version": "0.2.0",
  "configurations": [{
    "type": "qzjs",
    "request": "attach",
    "name": "qzjs: debug",
    "program": "${workspaceFolder}/app.js",
    "runtimeExecutable": "${workspaceFolder}/myapp",
    "runtimeArgs": ["${workspaceFolder}/app.js"],
    "env": { "QZ_DEBUG": "1" }
  }]
}
```
</details>

有了可用的适配器，你就能附加到入口处暂停的程序，继续以命中断点、
检查 Locals、单步、求值监视表达式。

## 当前可用功能（MVP）

- 按（源文件，行号）设置断点 — 在启动前从 VS Code 设置。
- 入口暂停（`stop_on_entry`）。
- 逐过程 / 步入 / 步出、继续。
- 调用栈，包含每个帧的文件/行号/函数。
- 局部变量作用域（参数 + 局部变量）及其值。
- `evaluate`（REPL/监视）。全局变量和纯表达式直接求值；帧的局部变量在求值期间暴露在 `locals` 对象上，因此 `locals.x` 读取局部变量。（裸写 `x` 不会绑定——真正的帧内求值需要 QuickJS 未暴露的引擎支持。）
- **暂停时世界冻结**：`fetch`/`setTimeout`/PAL 回调在暂停期间**不会**推进——暂停循环只服务调试协议请求（符合标准调试器「中断即冻结」语义）。

## 限制（MVP）

- **`evaluate` 裸局部变量绑定**：引用局部变量的监视表达式必须使用 `locals.` 前缀（`locals.x`，而非 `x`）。真正的帧内求值（直接绑定局部变量）需要 QuickJS 未暴露的引擎支持。
- **无 CDP / Chrome DevTools**：仅 DAP。Chrome DevTools 协议（通过 WebSocket 的 CDP）已推迟。
- **无 source map**，无条件/日志点断点，无异常断点，无编辑并继续，无多隔离。
- **`debugger;` 关键字**仍是无操作（断点从 UI 设置）。
- 注册 `qzjs` 调试类型的打包 VS Code 扩展是后续事项；DAP 层已完成并通过脚本化客户端测试。

## 暂停期间的异步

暂停时世界**按设计冻结**：暂停的 DAP 循环只服务调试协议请求（50ms stdin
轮询），不驱动 PAL 事件循环——暂停期间排队的 `fetch` 响应与 `setTimeout`
回调要等你 continue 之后才会触发。这符合标准调试器「中断即冻结」语义，
也避免 PAL 驱动的 JS 重入已停止的运行时（`debugger.c` 另有重入保护作
第二道防线）。

## 测试

```bash
cmake -B build -DQZ_BUILD_DEBUGGER=ON -DQZ_BUILD_TESTS=ON && cmake --build build -j$(nproc)
ctest --test-dir build -L dap --output-on-failure
```

`test/test_dap_gtest.cpp` 是一个进程内嵌入宿主，它 fork 一个子进程，在 `QZ_DEBUG=1` 下运行一个小型 JS 程序，然后通过管道充当 VS Code 客户端：initialize → setBreakpoints → configurationDone → 期望在断点处 `stopped` → stackTrace/scopes/variables/evaluate → step → continue → terminate。它验证了整个技术栈：引擎补丁 + 调试核心 + DAP 层 + `qz_create` 中的自动附加路径。
## 故障排查

**断点不命中 / 无 `stopped` 事件 / 测试 30 秒超时**

引擎的逐 opcode 断点检查由编译期宏门控。若 CMake 传给引擎的宏与
`deps/quickjs-ng-debugger.patch` 里的宏不一致，`DEBUGGER_CHECK` 会编译成
空操作——调试静默失效：不报错，断点就是不生效。

1. 核对两处宏名一致：

   ```bash
   grep -n DEBUG_SUPPORT deps/quickjs-ng-debugger.patch | head -4
   grep -n "QZ_DEBUG_SUPPORT_DEFINE" CMakeLists.txt
   ```

   两处必须同名（当前为 `QZ_DEBUG_SUPPORT`）。项目改名若漏改 patch，
   正是这种静默失效。

2. 确认引擎代码确实编入（未被编译剔除）：

   ```bash
   grep -c "js_debugger_check" deps/quickjs-ng/quickjs.c
   ```

3. 确认 DAP 层已链接（仅 `QZ_BUILD_DEBUGGER=ON` 时编入 `libqzjs`）：

   ```bash
   nm build/libqzjs.a 2>/dev/null | grep -c qz_dap_attach   # or build_dbg/libqzjs.a for the debugger build
   ```

4. 跑端到端客户端——通过则整栈没问题，问题在你的客户端协议交互：

   ```bash
   ctest --test-dir build -L dap --output-on-failure
   ```

**设置了 `QZ_DEBUG=1` 但程序不在入口暂停**

- THREAD 后端嵌入式宿主：auto-attach 发生在 qzjs 线程的 `qz_create`
  期间并阻塞等待 DAP 配置交换——客户端必须发送 `initialize` +
  `setBreakpoints` + `configurationDone`，否则 `qz_create` 永不返回。
- worker 运行时从不 auto-attach（每进程只有一份 stdio；worker 会与
  父 runtime 竞争 stdin）。断点只作用于被 attach 的那个 runtime。
- `config.debug = 1` **不是**调试位——用 `0x2`（位 1）。
