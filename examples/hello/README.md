# hello — 最简嵌入示例

创建运行时 → 执行 JS → 宿主与 JS 双向收发消息 → 销毁。整个示例只有一个
C 文件，是把 qzjs 嵌进宿主程序的最小可运行形态。

演示的核心契约：**宿主与运行时只经 JSON 消息通信**。

- 宿主 → JS：`qz_post_message(rt, json, len)`（线程安全）
- JS → 宿主：`postMessage(value)` 触发 `message_cb`（在 qzjs 线程上）

## 构建与运行

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DQZ_BUILD_EXAMPLES=ON
cmake --build build --target qz_hello
./build/examples/hello/qz_hello
```

## 期望输出

```
hello from qzjs!
JS 收到宿主消息: {"cmd":"ping"}
[host] 收到 JS 消息: {"greeting":"hello from JS","ts":...}
[host] 发消息给 JS: {"cmd":"ping"}
[host] 收到 JS 消息: {"reply":"pong"}
[host] 已销毁 runtime
```

## 要点

- `qz_config_t cfg = {0}` 零初始化；`cfg.message_cb` 是唯一必填项。
- `cfg.initial_script` 是启动即执行的 JS；`onmessage` 是 JS 侧的收信入口。
- 脚本在 qzjs 自己的线程上异步执行，示例用 `usleep` 等它跑完——真实宿主
  应改为在自己的事件循环里等 `message_cb`，而不是 sleep。
- `qz_create` 失败返回 `NULL`；示例按契约检查。
- 用 `QZ_RT_SERVER_PATH`（构建系统注入）告诉示例去哪找 `qzjs-rt`；默认
  进程模型是 ISOLATED，运行时跑在独立进程里。

## 相关文档

- [Quick Start](/guide/quickstart) — 从零构建并嵌入
- [Host Integration](/guide/host-integration) — 消息契约与双端事件分发
- [Runtime Lifecycle](/guide/lifecycle) — `qz_create` / `qz_destroy` 语义
