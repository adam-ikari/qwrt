---
title: 主机集成
description: 在 C 应用中嵌入 qwrt 的主机集成路径 —— create、JSON 消息契约、向 JavaScript 出借能力、优雅销毁。
---

# 主机集成

本页是向 C 应用嵌入 qwrt 的主干。它走完一个主机开发者会经历的全部闭环——
从创建运行时到销毁——并在每一步链接到深入页面。最需要内化的是**消息契约**：
qwrt 没有 `qwrt_eval`、没有 `qwrt_tick`。主机与运行时**只**通过 JSON 消息对话。

## 五步

```
┌─────────────────────────────────────────────────────────────┐
│ 1. create      qwrt_create(&cfg)   — 线程 + 循环 + JS 就绪    │
│ 2. script      initial_script / bytecode — JS 先跑什么        │
│ 3. communicate qwrt_post_message ⇄ message_cb  — JSON 契约   │
│ 4. lend        暴露 C 函数、serve/fs/worker/crypto 给 JS      │
│ 5. destroy     qwrt_destroy(rt)    — 优雅销毁                 │
└─────────────────────────────────────────────────────────────┘
```

## 1. Create

[`qwrt_create`](/zh/c-api/runtime) 启动 qwrt 内部线程、拉起 libuv 循环、
并执行 `cfg.initial_script`。它**阻塞直到就绪**——返回时运行时已活、`initial_script` 已运行。

```c
qwrt_config_t cfg = {0};
cfg.initial_script = "postMessage({ready: true});";
cfg.message_cb = on_message;      // 出站 JS→host
qwrt_t *rt = qwrt_create(&cfg);   // 阻塞直到就绪
```

## 2. 选择 JS 先跑什么

从轻到重有三种喂给运行时初始脚本的方式：

- **`initial_script`** —— 小字符串，适合引导逻辑
- **编译字节码** —— 用 `qjsc` 预编译，交付 `.bc`（启动更快、不携带源码）。见 [字节码](/zh/guide/bytecode)
- **`qwrt_post_message`** —— 创建后一切由消息驱动

## 3. 消息契约

这是 qwrt 的核心设计，也是多数主机第一次会搞错的地方。读两遍。

**没有直接 eval、没有 tick。** qwrt 拥有自己的线程与循环；你从不调用 JS 让它
运行，它也从不阻塞你的线程。

| 方向 | 机制 | 线程 |
|-----------|-----------|--------|
| 主机 → JS | `qwrt_post_message(rt, json, len)` | 线程安全，任意线程可调 |
| JS → 主机 | `cfg.message_cb(rt, json, len, data)` | 在 qwrt 线程上触发 |

规则：

- **两个方向都是 JSON 字符串。** 不跨边界传指针、不共享内存对象——只传可序列化数据。
- **`qwrt_post_message` 线程安全。** 可从任意主机线程调用；它入队到 qwrt 的入站队列。
- **`message_cb` 在 qwrt 线程上运行。** 保持快速且线程安全——它与事件循环及所有 JS 共享 qwrt 线程。
- **有界队列。** 若运行时繁忙（或 JS 从不读），入站消息在队列边界产生背压。
  设计你的主机要能应对 `qwrt_post_message` 不会立即排空。

```c
static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    // json 是完整 JSON 字符串；在主机侧解析并分发
    handle_json(json, len);
}

// 任意主机线程：
qwrt_post_message(rt, "{\"cmd\":\"start\",\"n\":42}", 22);
```

反方向——**JS 调 C**——是 JS 侧 `postMessage`（落到 `message_cb`）或把 C
函数注册为 JS 全局。见 [扩展](/zh/guide/extensions) 与深入页 [嵌入模式](/zh/guide/embedding)。

## 4. 向 JS 出借能力

边界建立后，运行时里的 JS 开箱即用 WinterTC 表面：`fetch`、`crypto.subtle`、
`ReadableStream`、timers、`fs`、`WebSocket`、`Worker`、`BroadcastChannel`、
`serve()`（HTTP/WS/gRPC 服务器）——全部是全局、无需 import。见 [JS API 参考](/zh/js-api/)。

此外你还能把自己的 C 函数注册为 JS 全局，让 JS 驱动产品的真实行为。见 [扩展](/zh/guide/extensions)。

## 5. Destroy

[`qwrt_destroy`](/zh/c-api/runtime) 执行优雅销毁：通知内部线程、排空待处理工作、
释放运行时。主机侧在运行时不再需要时调用。完整生命周期与内存模型见 [运行时生命周期](/zh/guide/lifecycle)。

---

## 深入

| 主题 | 页面 |
|-------|------|
| 线程所有权、就绪、关闭 | [运行时生命周期](/zh/guide/lifecycle) |
| 谁驱动循环、背压 | [事件循环](/zh/guide/event-loop) |
| 单运行时内多个隔离上下文 | [多上下文](/zh/guide/multi-context) |
| 注册 C 函数 / 结构化数据 | [嵌入模式](/zh/guide/embedding) |
| C API 参考 | [C API](/zh/c-api/) |
