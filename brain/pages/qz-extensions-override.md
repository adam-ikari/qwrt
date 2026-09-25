---
id: qz-extensions-override
title: "QZ_EXTENSIONS 覆盖：追加分隔符必须是空格"
category: decision
status: active
tags: [build, extensions, cmake]
created: "2026-09-24T02:54:03"
updated: "2026-09-24T02:54:17"
---

<!-- compiled_truth -->
- QZ_EXTENSIONS 是编译期扩展注册表（include/qzjs/qz_ext_registry.h），值是一个 C 宏表达式，展开成逗号分隔的 `const qz_ext_t *` 列表。
- QZ_DEFAULT_EXTENSIONS 宏本身以尾随逗号结尾（每个 QZ_EXT_IF_WITH(feature, ptr) 槽位展开为 "ptr," 或 "NULL,"）。
- 因此向默认集追加自定义扩展时必须用**空格**分隔：`-DQZ_EXTENSIONS="QZ_DEFAULT_EXTENSIONS &my_ext"`。
- 用逗号（`QZ_DEFAULT_EXTENSIONS, &my_ext`）会展开成 `ptr, , &my_ext`——空数组元素在 C99 下是硬编译错误（`error: expected expression before ',' token`，context.c.o 挂掉）。
- 自定义扩展还需 QZ_EXTRA_SOURCES（把 .c 编进 qzjs 目标，让 &my_ext 对 context.c 可见）与 QZ_EXTRA_HEADERS（-include 强制预包含 extern 声明）。
- 验证方法：全新 configure+build，`-DQZ_EXTRA_SOURCES=examples/extension/extension.c -DQZ_EXTRA_HEADERS=examples/extension/greet_ext.h -DQZ_EXTENSIONS='QZ_DEFAULT_EXTENSIONS &greet_ext'`，然后 `./build_ext/qzjs -e 'console.log(greet("qzjs"), greet(42))'` → `Hello, qzjs! Hello, 42!`。
- 命名注意：QZ_EXTENSIONS（注册哪些已编译进来的扩展）与 QZ_WITH_*（是否把扩展代码编进库）是两件事。


## Timeline

- time: 2026-09-24T02:54:03
  kind: decision
  summary: "Created this page: QZ_EXTENSIONS 覆盖：追加分隔符必须是空格"
  source: 2026-09 examples README sweep
  affects: [qz-extensions-override]

- time: 2026-09-24T02:54:17
  kind: decision
  summary: "首次固化：追加分隔符必须空格（尾随逗号陷阱）"
  source: "2026-09 examples README sweep，全新 configure+build 实测"
  affects: [qz-extensions-override]
