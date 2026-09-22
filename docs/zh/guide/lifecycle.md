---
title: 运行时生命周期
description: Amoib.js 运行时生命周期 — 创建、配置、使用和销毁。了解 am_create、am_destroy 以及消息循环。
---

# 运行时生命周期

每个 amoib 程序都遵循相同的生命周期：**创建 → 使用 → 销毁**。

## 创建运行时

```c
am_config_t config = {
    .initial_script = "postMessage('ready');",  // 在创建时于 amoib 线程上求值
    .message_cb = on_message,                   // 出站消息
    .debug = 0,                                 // 启用调试输出（0 或 1）
};
am_t *rt = am_create(&config);
if (!rt) {
    // 创建失败 — initial_script 抛出异常，或线程/循环初始化失败
}
```

`am_create` 执行以下操作：
1. 启动 amoib 的内部线程并初始化嵌入式 libuv 循环
2. 创建 JSRuntime 和初始上下文
3. 注册构建时扩展集（`AM_EXTENSIONS` 表 —
   内置扩展如 compress/crypto/textcodec/wamr，当其 `AM_WITH_*` 选项开启时生效，
   以及通过 `AM_EXTRA_SOURCES` 添加的任何用户扩展）
4. 将 WinterTC 兼容的运行时注入到初始上下文中
5. 在内部线程上求值 `initial_script` — 抛出异常会使 `am_create` 返回 `NULL`

`am_create` 会阻塞，直到内部线程就绪且 `initial_script` 已求值。运行时拥有其所有资源 — 没有需要保活的外部 PAL。

## 销毁运行时

```c
am_destroy(rt);  // 优雅关闭，仅宿主线程，NULL 安全
```

`am_destroy` 会：
1. 请求内部线程退出并 join 它
2. 销毁所有上下文（调用扩展的 `destroy` 钩子）
3. 释放 JSRuntime 和嵌入式 libuv 循环
4. 释放运行时

`am_destroy(NULL)` 是安全的（无操作）。

## 线程安全

- **所有 JS 在 amoib 的内部线程上运行** — 宿主线程从不调用 JS
- **`am_post_message` 是线程安全的** — 可从任何线程调用；JSON 会被拷贝
- **`message_cb` 在 amoib 线程上触发** — 你的回调必须线程安全
- **`am_destroy` 仅限宿主线程** — 从调用 `am_create` 的线程调用

## 内存模型

- 所有每运行时状态存储在 `am_t` 上 — **零可变文件作用域状态**
- QuickJS 类 ID 是运行时作用域的（在一个 `am_t` 内的各上下文之间共享）
- 通过 `am_get_rt_from_ctx(ctx)`（内部 API）从 `JSContext*` 恢复 `am_t*`
