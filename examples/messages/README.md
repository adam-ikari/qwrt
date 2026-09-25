# messages — JSON 请求/响应状态机

演示 qzjs 的核心架构：宿主与运行时只经 JSON 消息通信。示例跑一轮
"请求-响应"状态机——宿主发 `echo` / `add` / `date` 三条命令，JS 的
`onmessage` 按 `cmd` 字段分派并回复，`message_cb` 打印结果。

- 宿主 → JS：`qz_post_message(rt, json, len)`（线程安全，可任意线程调用）
- JS → 宿主：`postMessage(value)` 触发 `message_cb`（在 qzjs 线程上）

## 构建与运行

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DQZ_BUILD_EXAMPLES=ON
cmake --build build --target qz_messages
./build/examples/messages/qz_messages
```

## 期望输出

```
[host]  收到: {"ready":true}
[host]  发:  {"cmd":"echo","text":"hello from host"}
[host]  收到: {"ok":true,"echo":"hello from host"}
[host]  发:  {"cmd":"add","a":20,"b":22}
[host]  收到: {"ok":true,"sum":42}
[host]  发:  {"cmd":"date"}
[host]  收到: {"ok":true,"date":"..."}
[host]  发:  {"cmd":"bogus"}
[host]  收到: {"ok":false,"error":"unknown cmd: bogus"}
[host]  已销毁 runtime
```

## 要点

- **事件契约由你定义**：qzjs 只搬运 JSON，`cmd` 字段的语义、回复的
  `ok` 形状都是应用层约定。两端对称维护同一张事件表即可（见
  [Host Integration](/guide/host-integration) 的"双端事件分发"节）。
- `message_cb` 在 qzjs 线程上触发，要保持快且线程安全——重活丢回宿主
  自己的线程。
- 未知命令走 `ok:false` 分支，演示错误路径也是消息。
- 示例用 `usleep` 串行等待往返；真实宿主应在事件循环里等回调。

## 相关文档

- [Host Integration](/guide/host-integration) — 消息契约、双端分发
- [JS Execution](/guide/execution) — `initial_script` 与 `onmessage`
