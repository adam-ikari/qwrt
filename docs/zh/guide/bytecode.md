---
title: 字节码编译
description: qzjs 如何在内部用字节码（qjsc）加速启动 — 以及为什么没有公开的宿主侧字节码加载 API。
---

# 字节码编译

qzjs 在构建时用 `qjsc` 编译器把自己的 JavaScript（WinterTC polyfill 与
worker 启动脚本）**预编译为字节码**。加载字节码完全
跳过解析，从而加快启动并缩小发布体积。

## 内部用途

构建流水线把 polyfill 源码编译成字节码并嵌入二进制：

```bash
# qzjs 的构建对 WinterTC polyfill 与 worker 启动脚本执行此操作
qjsc -c polyfill.js -o polyfill_bytecode.c
```

运行时在内部线程上求值嵌入的字节码，而非解析源码。这是 qzjs 内部的优化 —
字节码由 qzjs 自己的源码生成，从不暴露给宿主。

## 没有公开字节码 API

`qzjs.h` 中**没有**公开的 `qz_compile` / `qz_eval_bytecode`，`qzjs` CLI
也没有字节码选项。宿主不能把字节码 blob 交给 qzjs 执行；JS 以源码形式通过
`initial_script`、消息或 `new Worker(url)` 脚本提供给运行时（见
[JS 执行](/zh/guide/execution)）。

唯一的字节码求值入口是内部的
（`qz_eval_bytecode_internal`，位于 `src/qz_internal.h`），供 qzjs
自身运行时与编译进 qzjs 的 C 扩展使用。如果你在编写这样的扩展可以使用它；
普通宿主嵌入无法使用。

## 字节码何时仍对你有帮助

如果启动延迟重要，你并不需要字节码 — 在 `initial_script` 中放一个小子脚本，
让 qzjs 预编译的 polyfill 承担成本。对于更大的应用脚本，比起手工调字节码，
更推荐把它们打包成单个文件（或 `new Worker` 脚本），因为公开接口没有
字节码路径。
