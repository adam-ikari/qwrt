---
title: 字节码编译
description: 在 Qwrt.js 中将 JavaScript 预编译为 QuickJS 字节码 — qjsc 工作流、polyfill 注入和字节码求值。
---

# 字节码编译

qwrt 可以将 JavaScript 编译为 QuickJS 字节码，以获得更快的启动速度和更小的部署体积。

## 为什么使用字节码？

- **更快的启动速度** — 运行时完全跳过解析阶段
- **更小的体积** — 字节码比源代码更紧凑
- **代码混淆** — 不发布源代码
- **预验证** — 语法错误在编译期捕获，而非运行时

## 将源代码编译为字节码

```c
const char *source = "function add(a, b) { return a + b; }";
size_t bytecode_len = 0;

uint8_t *bytecode = qwrt_compile(rt, source, strlen(source), &bytecode_len);
if (!bytecode) {
    // 编译失败 — 语法错误等
    return;
}

// 将字节码保存到文件、嵌入二进制文件等
// bytecode_len 是字节大小

qwrt_free(bytecode);
```

## 编译 ES 模块

```c
const char *module_source = "export function add(a, b) { return a + b; }";
size_t bytecode_len = 0;

uint8_t *bytecode = qwrt_compile_module(rt, module_source,
                                         strlen(module_source), &bytecode_len);
```

## 求值字节码

```c
char *result = NULL;
int ret = qwrt_eval_bytecode(rt, bytecode, bytecode_len, &result);
if (ret == 0) {
    printf("Result: %s\n", result);
    qwrt_free(result);
}
```

## 将字节码嵌入 C

你可以将字节码直接嵌入到二进制文件中：

```c
// 由以下命令生成：xxd -i polyfill.bytecode > polyfill_bytecode.h
#include "polyfill_bytecode.h"

void inject_polyfill(qwrt_t *rt) {
    qwrt_eval_bytecode(rt, polyfill_bytecode, polyfill_bytecode_len, NULL);
}
```

qwrt 自身的 WinterCG 模块使用此模式——它们在构建时预编译为 `src/polyfill_default.c`。

## 重新构建 WinterCG 模块

在编辑 JS 源文件后重新构建模块字节码：

```bash
cd polyfill
npm install          # 仅首次（拉取 esbuild）
npm run build        # 通过 esbuild 打包，使用 qjsc 编译
```

这将重新生成 `src/polyfill_default.c`——一个字节码的 C 数组，编译进 `libqwrt.a`。