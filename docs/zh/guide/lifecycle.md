---
title: 运行时生命周期
description: Qwrt.js 运行时生命周期 — 创建、配置、使用和销毁。了解 qwrt_create、qwrt_destroy、qwrt_reset 以及事件循环。
---

# 运行时生命周期

每个 qwrt 程序都遵循相同的生命周期：**创建 → 使用 → 销毁**。

## 创建运行时

```c
qwrt_config_t config = {
    .pal = pal,          // 平台抽象层（必需）
    .debug = 0,          // 启用调试输出（0 或 1）
};
qwrt_t *rt = qwrt_create(&config);
if (!rt) {
    // 创建失败 — 检查 PAL 初始化、内存等
}
```

`qwrt_create` 执行以下操作：
1. 如果提供了钩子，调用 `pal->init(pal)`
2. 创建 JSRuntime 和初始上下文
3. 注册构建时扩展集（`QWRT_EXTENSIONS` 表 —
   内置扩展如 compress/crypto/textcodec/wamr，当其 `QWRT_WITH_*` 选项开启时生效，
   以及通过 `QWRT_EXTRA_SOURCES` 添加的任何用户扩展）
4. 将 WinterCG 兼容的运行时注入到初始上下文中

PAL 必须比运行时存活更久。`qwrt_destroy` 不会释放 PAL — 调用者拥有 PAL 的所有权。

## 销毁运行时

```c
qwrt_destroy(rt);
// pal 仍然有效 — 调用者必须单独释放
pal_uv_destroy(pal);  // 或 pal_mock_destroy, pal_freertos_destroy 等
```

`qwrt_destroy` 会：
1. 销毁所有上下文（调用扩展的 `destroy` 钩子）
2. 排空所有剩余的延迟回调（但不执行它们）
3. 释放 JSRuntime
4. 如果提供了钩子，调用 `pal->destroy(pal)`

`qwrt_destroy(NULL)` 是安全的（无操作）。

## 重置运行时

重置会销毁所有上下文并创建新的初始上下文，保留相同的 PAL：

```c
qwrt_config_t new_config = { .pal = pal, .debug = 0 };
if (qwrt_reset(rt, &new_config) != 0) {
    // 重置失败
}
```

适用于以下场景：
- 清除所有 JS 状态而不重建 PAL
- 更改调试模式
- 从损坏的 JS 状态中恢复

## 线程安全

- **JSContext 绑定到线程**：所有 `qwrt_*` 调用必须来自调用 `qwrt_create` 的线程
- **无内部锁**：调用者负责线程纪律
- **PAL 回调**：在事件循环线程上触发；使用 `qwrt_defer_callback` 安全地分发到 JS 线程

## 内存模型

- 所有每运行时状态存储在 `qwrt_t` 上 — **零可变文件作用域状态**
- QuickJS 类 ID 是运行时作用域的（在一个 `qwrt_t` 内的各上下文之间共享）
- 通过 `qwrt_get_rt_from_ctx(ctx)`（内部 API）从 `JSContext*` 恢复 `qwrt_t*`