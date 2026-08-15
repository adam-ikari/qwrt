# qwrt CLI — 独立运行完整 JS 运行时

日期：2026-08-15
状态：设计已获用户批准

## 背景与目标

qwrt 目前只作为 SDK 嵌入 C 程序（`libqwrt.a` + 公开头文件 + examples）。本设计补齐第二种运行形态：**独立运行**——一个名为 `qwrt` 的可执行文件，以 node 风格直接运行 JS 脚本，脚本内可完整使用 qwrt 的全部 Web API 能力（fetch、crypto.subtle、ReadableStream、setTimeout、fs、URL、Worker 等 21 个 WinterTC 模块）。

定位对比：quickjs-ng 自带的 `qjs` 只是裸 ECMAScript 引擎壳（无 Web API），不是完整运行时；`qwrt` CLI 的目标是让宿主具备完整 Web API 的脚本能独立跑起来，与 SDK 嵌入模式共享同一套运行时内核。

## 需求（已确认）

| 维度 | 决定 |
|---|---|
| 形态 | CLI 工具，可执行名 `qwrt`（与库同名，对标 node 的命名身份） |
| 定位 | 完整 JS 运行时：全部 Web API 模块可用；比 qjs（裸引擎壳）完整 |
| MVP 范围 | 单文件脚本运行；**不做**模块系统/包解析（node_modules、import 第三方） |
| 接口 | `qwrt script.js` 运行文件；`qwrt -e 'code'` 执行片段；无参数进 REPL；`--help` / `--version`；脚本内 `process.argv`；退出码 |
| 异步退出语义 | 顶层代码跑完后等待所有 pending 异步任务（fetch/timer/streams）完成再退出；无 pending 才退出（node 风格） |

## 方案选择

候选：
1. **C 侧 CLI**（采用）— 新增 `src/cli.c` + 可执行 target `qwrt`，用 qwrt 库的 C API（`qwrt_create` / `qwrt_post_message` / `message_cb`）驱动。CLI 是 SDK 的第一个真实消费者（dogfooding），libuv 生命周期控制最自然。
2. JS 侧 CLI — C 壳只做 bootstrap，主逻辑用 JS；REPL/arg 解析写起来自然，但需新增 process 桥，bootstrap 复杂度高。
3. 改造 qjs — 复用 quickjs-ng 自带 qjs 可执行注入 Web API；改动最小但生命周期不是 qwrt 的 libuv 模型，退出语义难控制，不 dogfood。

## 架构

```
┌──────────────────────────────────────────────────┐
│ src/cli.c  (可执行 target: qwrt)                  │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────┐  │
│  │ arg 解析器   │→ │ 脚本加载器    │→ │ 消息通道 │  │
│  │ (main)      │  │ (读文件/-e)   │  │ post/   │  │
│  └─────────────┘  └──────────────┘  │ message_cb│ │
│  ┌─────────────┐  ┌──────────────┐  └─────────┘  │
│  │ REPL 循环    │  │ process 桥    │  ┌─────────┐  │
│  │ (行读取→eval)│  │ (argv/env/   │  │ 生命周期  │  │
│  │             │  │  exit 注入)   │  │ 管理器   │  │
│  └─────────────┘  └──────────────┘  └─────────┘  │
└──────────────────────┬───────────────────────────┘
                       │ qwrt C API (qwrt_create / post_message / message_cb)
             ┌─────────▼──────────┐
             │  qwrt 内核           │  ← 与 SDK 嵌入共享同一内核
             │  (libuv loop +      │     (libqwrt.a / libqwrt_full.a)
             │   QuickJS-ng +      │
             │   21 Web API 模块)  │
             └────────────────────┘
```

### 组件职责

- **arg 解析器**：解析 argv → 模式（script / -e / REPL）+ flags（--help / --version）。未知 flag 打印 usage 到 stderr，退出码 2。
- **脚本加载器**：读文件（不存在 → stderr + 退出码 1）或取 `-e` 参数，构造 eval 消息。
- **消息通道**：复用宿主契约——`qwrt_post_message` 发 eval 命令，`message_cb` 收结果与 console 输出。与 test_host.h 的 HostCtx 模式同构。
- **REPL**：无参数时启动；行读取 → eval → 打印结果/错误；EOF（Ctrl-D）退出。MVP 用简单行读取（getline/fgets），不做历史/补全。
- **process 桥**：通过 initial_script 注入全局 `process` 对象：`process.argv`（脚本路径 + 剩余参数）、`process.env`、`process.exit(code)`。
- **生命周期管理器**：libuv 事件循环保持 pending 异步任务（fetch/timer/streams 持有活跃 handle）→ 顶层 eval 返回后 loop 继续跑直到空 → 自然等待语义；`process.exit(n)` 强制终止。

## 数据流

**脚本模式**：main 解析 args → `qwrt_create(cfg, initial_script 注入 process)` → post `{cmd:"eval", code}` → message_cb 收到结果/console 输出 → 打印到 stdout → pending 异步任务完成后 loop 空 → 退出码 0（成功）/ 1（脚本异常）。

**REPL 模式**：每行输入 → post eval → message_cb 回传 → 打印求值结果或错误 → 循环直到 EOF。

## 错误处理

| 场景 | 行为 |
|---|---|
| 脚本文件不存在 | stderr 报错，退出码 1 |
| 脚本语法/运行时异常 | 错误打印到 stderr，退出码 1 |
| 未知 flag / 缺参数 | usage 打印到 stderr，退出码 2 |
| `process.exit(n)` | 立即以 n 退出 |
| `--help` / `--version` | 打印到 stdout，退出码 0 |

## 测试策略

新增 `test/test_cli_gtest.cpp`：fork `qwrt` 可执行，断言 stdout/stderr/退出码（与 test_dap_gtest 的子进程驱动模式一致）。

用例：
1. `hello.js`（console.log）→ stdout 含输出，退出码 0
2. `-e '1+2'` → 输出结果，退出码 0
3. fetch 异步等待：脚本发起 fetch 后顶层结束 → 进程等 fetch 完成再退出（异步退出语义）
4. 脚本异常 → stderr 含错误，退出码 1
5. 文件不存在 → stderr 报错，退出码 1
6. `--help` / `--version` → 退出码 0
7. `process.argv` 含脚本路径与额外参数
8. `process.exit(3)` → 退出码 3

## 构建集成

- CMake 新增 `add_executable(qwrt src/cli.c)`，链接 `libqwrt_full`（含全部扩展：crypto/compress/textcodec/wasm）——与“完整运行时”定位一致；若构建配置显式关闭某扩展，链接按 QWRT_WITH_* 对应裁剪。
- 不破坏现有 SDK 构建；examples 保持独立。
- CLI target 命名 `qwrt`，可执行输出到构建目录。

## 明确不做（YAGNI）

- 模块系统 / node_modules 解析 / CommonJS（后续迭代）
- REPL 历史记录、Tab 补全（后续迭代）
- `--watch`、`--inspect`（后续迭代）
- 包管理（npm 生态对接）
