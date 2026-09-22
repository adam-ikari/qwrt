# 扩展模块注册机制重设计

**日期**: 2026-07-09
**状态**: 设计稿，待使用者复核

## 目标

给 amoib 使用者**一种**明确、非侵入式的方式来：
1. **定制启用**的扩展模块（开启/关闭/重排）。
2. **添加**自己的扩展模块到工程中。

少即是多——单一控制点，不做多套机制。

## 设计约束

- 静态扩展，**编译期控制**（不做 dlopen 真动态加载；"动态库/文件系统加载"
  的说法已校准为心智模型，实际是静态链接、编译期确定）。
- **统一方式**：所有扩展经同一张编译期宏表，内置与用户扩展地位平等。
- **一种控制方式**：单一宏 `AM_EXTENSIONS`，同时表达开启/关闭/追加/重排。
- **非侵入式**：使用者通过**父工程 CMake 宏定义覆盖** amoib CMake 中的同名宏，
  不改 amoib 的任何源码或 CMake 文件。定制 = 重编 amoib 库（已确认接受）。
- **不保留旧方式**：删除 `am_config_t.extensions`、`am_register_ext()`、
  `context.c` 的硬编码 auto-register、per-context 堆分配数组与
  `extensions_dynamic`、`am_ext_t.version`。
- 所有平台（uv/mock/freertos）控制方法完全一致。
- 服从 amoib 现有规矩：C99、无文件级可变全局状态、standalone（无系统库）、
  所有 deps 用 `add_subdirectory(... EXCLUDE_FROM_ALL)`。

## 本 spec 范围（一个 spec 全做）

1. 扩展注册机制重设计（`AM_EXTENSIONS` 宏表 + 父工程覆盖）。
2. **wamr 设为默认 WASM 引擎**（替代 wasm3 默认）。
   - 需把 `deps/wamr` 注册为 git 子模块并 pinned 版本。
   - 需让 amoib CMake 从源码自动构建 wamr 的 `libvmlib.a`
     （`add_subdirectory` 或等价方式，**禁止 `execute_process`**）。
   - **可行性待验证**：wamr 仓库的 CMake 结构是否支持 `add_subdirectory`
     自动构建，是本方案的硬前提。若不支持，需在实现阶段设计替代构建方案
     （如 vendored 的 wamr CMake wrapper），此点标注为**风险**。

## 对外契约：唯一控制方式

### 新增公开头文件 `include/amoib/am_ext_registry.h`

是扩展控制的**唯一入口**。

```c
#ifndef AM_EXT_REGISTRY_H
#define AM_EXT_REGISTRY_H

#include <amoib/amoib.h>   /* am_ext_t */

/* AM_EXT_IF_WITH(FEATURE, ptr) 在 AM_WITH_<FEATURE>=1 时产出 "ptr,"，
 * 否则产出 "NULL,"（一个空指针占位）。关键：永远产出"非空 + 逗号"，
 * 不产出空串——因为 C99 数组初始化器里 ", ,"（空元素）是编译错误，
 * 而末尾多余逗号合法。故禁用的扩展以 NULL 占位，am_ext_init_all 跳过 NULL。
 *
 * 需要三级宏间接，使 FEATURE 名先展开成 AM_WITH_<FEATURE> 的值(0/1)，
 * 再粘贴成 AM_EXT_IF_WITH0/1 —— 两级会因 ## 阻止参数展开而失败
 * （已原型验证，见下）。 */
#define AM_EXT_IF_WITH0(ptr)   NULL,
#define AM_EXT_IF_WITH1(ptr)   ptr,
#define AM_EXT_IF_WITH_3(bit, ptr) AM_EXT_IF_WITH##bit(ptr)
#define AM_EXT_IF_WITH_2(bit, ptr) AM_EXT_IF_WITH_3(bit, ptr)
#define AM_EXT_IF_WITH_1(feature, ptr) AM_EXT_IF_WITH_2(AM_WITH_##feature, ptr)
#define AM_EXT_IF_WITH(feature, ptr) AM_EXT_IF_WITH_1(feature, ptr)

/* amoib 预置的扩展符号。默认集 = CMake 默认 ON 且开箱即用的扩展。
 * wamr 设为默认 WASM 引擎后，wamr 进默认集；wasm3 不进（用户显式写）。
 * compress/crypto/textcodec/wamr 各自走条件宏。 */
#define AM_DEFAULT_EXTENSIONS \
    AM_EXT_IF_WITH(COMPRESS,   &am_compress_ext) \
    AM_EXT_IF_WITH(CRYPTO_EXT, &am_crypto_ext)   \
    AM_EXT_IF_WITH(TEXTCODEC,  &am_textcodec_ext) \
    AM_EXT_IF_WITH(WAMR,       &am_wamr_ext)

/* 实际生效的扩展集。可被：
 *   (a) 父工程 CMake 覆盖（推荐，非侵入）；
 *   (b) 翻译单元 #define 覆盖；
 * 不定义则用默认集。注册顺序 = 此宏展开的书写顺序。 */
#ifndef AM_EXTENSIONS
#  define AM_EXTENSIONS AM_DEFAULT_EXTENSIONS
#endif

#endif /* AM_EXT_REGISTRY_H */
```

> **注**：`AM_EXT_IF_WITH` 要求 `AM_WITH_<FEATURE>` 是 `0` 或 `1` 的整数值
> 宏（amoib CMake 以 `target_compile_definitions(... AM_WITH_WAMR=1)` 注入）。
> 内置扩展的 `AM_WITH_*` 恒为定义的 0/1；用户扩展不受此机制约束（用户在
> `AM_EXTENSIONS` 里直接写 `&my_foo_ext`）。
>
> **NULL 占位与计数**：禁用的内置扩展在表中以 `NULL` 槽出现（条件宏产出）。
> 表**不用 NULL 终止符**（占位 NULL 会与终止 NULL 混淆），改用 `sizeof/sizeof`
> 计算元素数，迭代时跳过 `NULL` 槽。C99 允许数组初始化器末尾多余逗号，故无论
> `AM_EXTENSIONS` 末项是否带尾逗号都能编译：内置集恒带（条件宏产出 `ptr,`），
> 用户裸列表带或不带皆可。见"内部实现"。

`AM_EXTENSIONS` 展开为以逗号分隔的 `const am_ext_t *` 列表（末项尾逗号
可选；内置集恒带，用户裸列表带或不带皆可）。

**所有控制全靠改这一个宏**，没有第二套机制：

| 操作 | 写法 |
|------|------|
| 用默认 | 不做任何事 |
| 关闭某项 | `AM_EXTENSIONS="&am_crypto_ext,&am_textcodec_ext,&am_wamr_ext"`（去掉 compress） |
| 追加自己的 | `AM_EXTENSIONS="AM_DEFAULT_EXTENSIONS,&my_foo_ext"` |
| 只用某些 | `AM_EXTENSIONS="&am_compress_ext"` |
| 换 WASM 引擎为 wasm3 | `AM_EXTENSIONS="&am_compress_ext,&am_crypto_ext,&am_textcodec_ext,&am_wasm3_ext"`（把 wamr 换成 wasm3，且 CMake 开 `AM_WITH_WASM3=ON`/关 `AM_WITH_WAMR=OFF`） |

### 使用者工程模型（非侵入式的核心）

amoib 的 CMake 把 `AM_EXTENSIONS` 做成**可被父工程覆盖的变量**：amoib 检测
父工程是否已设值，已设则用父工程的值、未设则用 `AM_DEFAULT_EXTENSIONS`，
再以 `target_compile_definitions(amoib PRIVATE ...)` 注入到 `context.c`。

**父工程非侵入式定制**（不改 amoib 源码/CMake）：

```cmake
# 使用者工程的 CMakeLists.txt
set(AM_EXTENSIONS "AM_DEFAULT_EXTENSIONS,&my_foo_ext")
add_subdirectory(deps/amoib)   # amoib 检测到父工程已设 AM_EXTENSIONS，采用之
```

或命令行：`cmake -DAM_EXTENSIONS="&am_compress_ext" ...`。

使用者添加自己的扩展 = 在自己工程里写 `ext_foo.c` 暴露
`const am_ext_t am_foo_ext`，并把该源文件**编入 amoib 静态库**
（见下"用户扩展的符号可见性"），再在 `AM_EXTENSIONS` 里列 `&am_foo_ext`。
**不改 amoib 的任何源码或 CMake 文件。**

### 用户扩展的符号可见性（非侵入式添加的关键）

`AM_EXTENSIONS` 展开成 `&am_foo_ext` 这样的指针，须在 `context.c`
翻译单元里可见为符号——而 `am_foo_ext` 定义在使用者工程的源码里。解法：

**amoib CMake 暴露一个变量 `AM_EXTRA_SOURCES`**，父工程在
`add_subdirectory(amoib)` 之前 `set(AM_EXTRA_SOURCES src/ext_foo.c)`，
amoib 把这些源加入 `amoib` target：

```cmake
# 使用者工程
set(AM_EXTENSIONS "AM_DEFAULT_EXTENSIONS,&am_foo_ext")
set(AM_EXTRA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/ext_foo.c)
add_subdirectory(deps/amoib)
```

```cmake
# amoib CMakeLists.txt 内
if(DEFINED AM_EXTRA_SOURCES)
    target_sources(amoib PRIVATE ${AM_EXTRA_SOURCES})
endif()
```

这样 `ext_foo.c` 与 `context.c` 同属 `amoib` target，`&am_foo_ext` 自然
可见，且使用者未改 amoib 任何文件。`AM_EXTRA_SOURCES` 与
`AM_EXTENSIONS` 配合使用：前者让符号可见，后者把它纳入注册表。

### `AM_EXTENSIONS` 与 `AM_WITH_*` 的关系

- `AM_WITH_*`（CMake 选项）= 该扩展代码**有没有编译进库**（链接期）。
- `AM_EXTENSIONS`（宏）= 此构建里**注册哪些**已编译进库的扩展（使用期）。
- 默认 `AM_EXTENSIONS` 列出 compress/crypto/textcodec/wamr（wamr 设默认后）。
- 若宏里引用未编译进库的扩展符号 → 链接期"未定义符号"错误，是期望保护。
- **wamr 关闭时的默认集**：`AM_DEFAULT_EXTENSIONS` 列了 `&am_wamr_ext`，
  若用户 `AM_WITH_WAMR=OFF`，则该符号未定义 → 默认集会链接失败。处理：
  `am_ext_registry.h` 用 `AM_EXT_IF_WITH(WAMR, &am_wamr_ext)` 之类条件宏，
  仅在 `AM_WITH_WAMR` 定义时才产出该项。内置扩展的默认集
  一律走条件宏，保证"关掉对应 `AM_WITH_*` 时默认集自动剔除该项"。

## wamr 设为默认 WASM 引擎

### CMake 改动

- `option(AM_WITH_WAMR ...)` 默认改为 **ON**；`option(AM_WITH_WASM3 ...)`
  默认改为 **OFF**（互斥：二者不应同时默认 ON；保留可显式同时开以供切换，
  但 `AM_DEFAULT_EXTENSIONS` 只列 wamr）。
- `deps/wamr` 注册为 git 子模块，pinned 版本（实现时选定 tag）。
- wamr 的 `libvmlib.a` 改为**自动从源码构建**（替代当前的手工预构建 +
  FATAL_ERROR）。方式优先 `add_subdirectory(deps/wamr ... EXCLUDE_FROM_ALL)`；
  **风险**：若 wamr 的 CMake 不支持被 `add_subdirectory`，则需 vendored
  wrapper 或 `ExternalProject_Add`（CLAUDE.md 禁 `execute_process`，但
  `ExternalProject_Add` 在构建期运行，需确认是否可接受；此为待决项）。
- CI 矩阵更新：原 wasm3 默认的 job 改为 wamr 默认；保留 wasm3 可选 job。

### 互斥语义

- wasm3 与 wamr 不应在 `AM_EXTENSIONS` 里同时出现（二者都注册 `WebAssembly`
  全局会冲突）。`am_ext_init_all` 加**廉价开发期断言**：检测到 wasm3 与 wamr
  同时在表里时 `am_debug` 警告。不强制阻止（用户若知道自己在干嘛）。

## 内部实现

### `src/context.c`

删除现有三段 `#ifdef AM_WITH_COMPRESS/CRYPTO_EXT/TEXTCODEC` auto-register
块（含 O(n) 指针去重扫描）。改为：

```c
#include "am_ext_registry.h"

/* 编译期扩展表。含若干 NULL 占位（被禁用的内置扩展），由迭代方按 count 跳过。
 * 不用 NULL 终止符（占位 NULL 与终止 NULL 无法区分），改用 sizeof 计数。
 * 末项无论是否带尾逗号均合法（C99 允许初始化器末尾多余逗号）。 */
static const am_ext_t *default_exts[] = {
    AM_EXTENSIONS
};
static const int default_exts_count =
    (int)(sizeof(default_exts)/sizeof(default_exts[0]));
```

迭代时按 `default_exts_count` 范围遍历、跳过 `NULL` 槽。`am_ctx_t.extensions`
改为存指针 + count 二元组（见下）。`static const` 文件作用域、不可变、零运行时
开销。不再动态增长，不再去重。

### `src/extension.c`

- 删除 `am_ext_register()`。
- 保留 `am_ext_init_all` / `destroy_all` / `suspend_all` / `resume_all`：
  按 `ctx->extensions_count` 范围迭代，跳过 `NULL` 槽，不再 malloc/realloc。

### `src/am_internal.h` 的 `am_ctx_t`

```c
const am_ext_t * const *extensions;  /* 指向编译期表，只读 */
int extensions_count;                    /* 表长（不含哨兵），迭代用 */
/* 删除 extensions_dynamic 字段 */
```

### `src/amoib.c` 与 `include/amoib/amoib.h`

- 删除公共 API `am_register_ext()`。
- `am_config_t` 删除 `extensions` 字段，保留 `pal` 与 `debug`。
- `am_create` 不再读 `config->extensions`；用 `AM_EXTENSIONS` 表。
- `am_ext_t` 删除 `version` 字段；`name` 保留（诊断用）。

### CMake 注入宏

amoib CMake 在配置 `amoib` target 时：

```cmake
# 父工程可覆盖 AM_EXTENSIONS；未设则由头文件默认为 AM_DEFAULT_EXTENSIONS
if(DEFINED AM_EXTENSIONS)
    target_compile_definitions(amoib PRIVATE AM_EXTENSIONS=${AM_EXTENSIONS})
endif()
```

注意：若父工程设的是 `AM_DEFAULT_EXTENSIONS,&my_foo_ext` 这样的"含宏引用"
字符串，`context.c` 仍需 `#include "am_ext_registry.h"` 以解析
`AM_DEFAULT_EXTENSIONS`。即覆盖值可以是宏表达式的文本。

## 顺序、去重、错误处理

- **顺序**：`AM_EXTENSIONS` 展开顺序即注册顺序。默认集顺序固定
  （compress→crypto→textcodec→wamr）；用户覆盖时可任意重排。
- **去重**：编译期宏是唯一来源，**不做运行时去重**。同一扩展写两次 → `init`
  跑两次，属用户错误。`am_ext_init_all` 加**廉价 O(n²) 开发期断言**（重复
  `name` 或 wasm3/wamr 同时存在时 `am_debug` 警告；Release 可编译为空）兜底，
  不改变注册行为。
- **init 失败**：返回 -1 并回滚已注册扩展（保留现有逻辑）。
- **引用未编译进库的扩展符号**：链接期"未定义符号"错误，提示明确。

## 迁移与影响面

受影响文件：

- 新增 `include/amoib/am_ext_registry.h`
- 改 `src/context.c`：删 auto-register 块，改用宏表
- 改 `src/extension.c`：删 `am_ext_register`
- 改 `src/am_internal.h`：`am_ctx_t` 字段调整，删 `extensions_dynamic`
- 改 `src/amoib.c`、`include/amoib/amoib.h`：删 `am_register_ext`、
  `config.extensions`、`am_ext_t.version`
- 改 `CMakeLists.txt`：wamr 默认 ON + 自动构建 + wasm3 默认 OFF + `AM_EXTENSIONS`
  注入 + `AM_EXTRA_SOURCES` 支持；`.gitmodules` 加 wamr
- 改 `examples/`、`test/`：用旧 API 的地方改

**破坏性变更**（已确认可接受）：

- `am_config_t` 删 `extensions` 字段。
- 公共 API `am_register_ext()` 删除。
- `am_ext_t` 删 `version` 字段。
- 默认 WASM 引擎从 wasm3 改为 wamr。

## 风险

1. **wamr 自动构建可行性未验证**（最大风险）。wamr 的 CMake 是否支持
   `add_subdirectory` 自动构建 `libvmlib.a`，需在实现阶段验证。不支持则需
   wrapper/ExternalProject，工作量与 CLAUDE.md 合规性待定。
2. ~~`AM_EXTENSIONS` 含宏引用字符串~~ — **已原型验证解决**：三级宏间接
   （`AM_EXT_IF_WITH`）在 `-std=c99 -Wall -Wextra -Werror` 下编译通过，
   禁用项→NULL 占位、用户追加、count 计数均正确运行。`AM_DEFAULT_EXTENSIONS`
   作为父工程覆盖值的一部分（`AM_DEFAULT_EXTENSIONS,&my_foo_ext`）经
   `context.c` 的 `#include "am_ext_registry.h"` 正常展开。符号可见性已由
   `AM_EXTRA_SOURCES` 机制解决。

## 测试

- `test/test_extension_gtest.cpp`：删去重/动态注册用例；保留默认集注册、
  init/destroy 正常。
- `AM_EXTENSIONS` 覆盖用例：以**独立翻译单元/独立 CMake target** 验证
  （单独 `.c` + `-DAM_EXTENSIONS=...`），避免与主 TU 宏冲突。
- wamr 默认构建：CI job 验证 wamr 默认 ON 时 `WebAssembly` 可用且 wasm3 不在
  默认集。
