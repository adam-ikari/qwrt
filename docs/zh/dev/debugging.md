---
title: 调试
description: 使用 DAP 调试器调试 Qwrt.js — 断点、步进、变量检查以及 VS Code 集成。
---

# 使用 VS Code 调试 qwrt 程序

qwrt 内置了一个 **DAP（调试适配器协议）** 步进调试器，直接编译在库本身中——无需单独的调试器二进制文件。启用后，任何嵌入 qwrt 的程序都可以在 VS Code 中进行步进调试（断点、逐过程/步入/步出、调用栈、局部变量、求值）。

## 工作原理

调试器是一种**库能力**，而非独立进程。它位于 `src/debugger.c`（调试核心）和 `src/debugger_dap.c`（DAP 协议层）中，当 `QWRT_BUILD_DEBUGGER=ON` 时编译进 `libqwrt.a`。对 QuickJS-ng 引擎的一个小补丁（`deps/quickjs-ng-debugger.patch`）添加了调试核心使用的断点/步进内省原语。

激活方式为**通过配置或环境变量自动**——你的宿主代码无需更改。`qwrt_create` 检查调试设置，如果启用，则附加 DAP 层（通过 stdin/stdout 进行 DAP 通信）并在入口处暂停。VS Code 随后附加。

### 双层禁用（关闭时零开销）

- `QWRT_BUILD_DEBUGGER=OFF`（默认）：**不**应用引擎补丁，**不**编译 `src/debugger.c`/`src/debugger_dap.c`，`qwrt_create` 中没有调试代码路径。调试功能不存在；`libqwrt.a` 保持不变。
- `QWRT_BUILD_DEBUGGER=ON`：应用补丁并编译源文件，但引擎的每个操作码的 `DEBUGGER_CHECK` 是无操作的（一个永不执行的分支），**除非运行时附加了调试器**。非调试运行几乎没有性能开销。

## 构建

```bash
cmake -B build -DQWRT_BUILD_DEBUGGER=ON -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

这会在配置时将 `deps/quickjs-ng-debugger.patch` 应用到 QuickJS-ng 子模块的工作树（子模块在 git 中保持干净——补丁是事实来源）。`cmake -DQWRT_BUILD_DEBUGGER=OFF` 恢复原始状态。

## 在你的程序中启用调试

**方案 A — 无需代码更改（环境变量）：** 使用 `QWRT_DEBUG=1` 运行你的程序：

```bash
QWRT_DEBUG=1 ./myapp app.js
```

**方案 B — 配置位：** 设置 `qwrt_config_t.debug` 的位 1（位 0 是现有的详细日志标志）：

```c
qwrt_config_t cfg = { .pal = pal, .debug = 0x2 };  /* 位 1 = 启用调试 */
qwrt_t *rt = qwrt_create(&cfg);
qwrt_eval(rt, src, NULL);   /* 在入口处暂停，然后在断点处暂停 */
```

就这样——`qwrt_create` 自动附加 DAP，发送 `initialized`，并在 DAP 配置阶段（initialize / setBreakpoints / configurationDone）阻塞后返回。`stop_on_entry` 在程序的第一条语句处暂停。

## VS Code 设置

你的程序是调试目标——VS Code 的 `runtimeExecutable` 指向**你的**二进制文件，而非 qwrt 提供的。创建 `.vscode/launch.json`：

```json
{
  "version": "0.2.0",
  "configurations": [{
    "type": "qwrt",
    "request": "attach",
    "name": "qwrt: debug",
    "program": "${workspaceFolder}/app.js",
    "runtimeExecutable": "${workspaceFolder}/myapp",
    "runtimeArgs": ["${workspaceFolder}/app.js"],
    "env": { "QWRT_DEBUG": "1" }
  }]
}
```

> **注意：** `type: "qwrt"` 需要一个注册了 `qwrt` 调试类型的 VS Code 扩展。在打包的扩展发布之前，你可以直接驱动 DAP 层（适配器通过 stdio 使用标准 DAP）或使用脚本化测试（`test/test_dap_debugger.c`）作为参考客户端。DAP 层实现了：initialize、attach、setBreakpoints、configurationDone、threads、stackTrace、scopes、variables、continue、next、stepIn、stepOut、evaluate、disconnect。

在源代码中设置断点，按 F5，VS Code 会附加到在入口处暂停的程序。继续执行以命中断点；检查局部变量、步进、求值监视表达式。

## 当前可用功能（MVP）

- 按（源文件，行号）设置断点 — 在启动前从 VS Code 设置。
- 入口暂停（`stop_on_entry`）。
- 逐过程 / 步入 / 步出、继续。
- 调用栈，包含每个帧的文件/行号/函数。
- 局部变量作用域（参数 + 局部变量）及其值。
- `evaluate`（REPL/监视）。全局变量和纯表达式直接求值；帧的局部变量在求值期间暴露在 `locals` 对象上，因此 `locals.x` 读取局部变量。（裸写 `x` 不会绑定——真正的帧内求值需要 QuickJS 未暴露的引擎支持。）
- **暂停期间异步推进**：`fetch`/`setTimeout` 在暂停时继续推进（DAP 循环在 stdin 轮询之间泵送 PAL 事件循环，单线程）。

## 限制（MVP）

- **`evaluate` 裸局部变量绑定**：引用局部变量的监视表达式必须使用 `locals.` 前缀（`locals.x`，而非 `x`）。真正的帧内求值（直接绑定局部变量）需要 QuickJS 未暴露的引擎支持。
- **无 CDP / Chrome DevTools**：仅 DAP。Chrome DevTools 协议（通过 WebSocket 的 CDP）已推迟。
- **无 source map**，无条件/日志点断点，无异常断点，无编辑并继续，无多隔离。
- **`debugger;` 关键字**仍是无操作（断点从 UI 设置）。
- 注册 `qwrt` 调试类型的打包 VS Code 扩展是后续事项；DAP 层已完成并通过脚本化客户端测试。

## 异步支持

调试器**确实**在暂停时推进异步 JS。当在断点处停止时，DAP 层的 `on_stopped` 循环以短超时轮询 stdin，并在轮询之间驱动一次非阻塞的 PAL 事件循环迭代（`pal->run_cycle(0)` + `qwrt_tick`）。因此 `fetch` 响应、`setTimeout` 回调等在你检查暂停状态时继续触发——全部在单个 JS 线程上（qwrt 不拥有任何线程）。重入保护可防止 PAL 驱动的 JS 嵌套另一次停止。

`test/test_dap_async.c` 验证了这一点：一个 uv 支持的被调试程序调度一个 100ms 的 `setTimeout`，命中断点，定时器在暂停期间触发（循环以 `n=1` 退出，而非旋转到上限）。

## 测试

```bash
cd build && ctest -R test_dap_debugger --output-on-failure
```

`test/test_dap_debugger.c` 是一个进程内嵌入宿主，它 fork 一个子进程，在 `QWRT_DEBUG=1` 下运行一个小型 JS 程序，然后通过管道充当 VS Code 客户端：initialize → setBreakpoints → configurationDone → 期望在断点处 `stopped` → stackTrace/scopes/variables/evaluate → step → continue → terminate。它验证了整个技术栈：引擎补丁 + 调试核心 + DAP 层 + `qwrt_create` 中的自动附加路径。