# WAMR AOT 旁路设计 — 解释器/AOT 双路径选择 + 回退

> 状态：设计决策（只读探查产出，未改 src/，未验证运行）
> 日期：2026-09-02
> 范围：WAMR 引擎（`QWRT_WITH_WAMR`）的 WebAssembly API 层。wasm3 / 浏览器原生（`ext_web_wasm.c`）路径不在本设计内。

**Goal:** 让 `WebAssembly.instantiate` / `compile` / `compileStreaming` / `instantiateStreaming` 等在解释器（Fast Interpreter）与 AOT（wamrc 预编译）之间**自动选择**：缺 .aot、AOT 加载失败、运行时生成的模块 → **静默回退解释器**，调用方无感知；vmlib 保持**纯 C、无 C++ 运行时依赖**。

---

# 1. 现状（只读探查，2026-09-02）

## 1.1 WAMR AOT 已编译进 vmlib

`CMakeLists.txt:309-320`（QWRT_WITH_WAMR 分支）：

```
WAMR_BUILD_INTERP 1
WAMR_BUILD_AOT 1
WAMR_BUILD_JIT 0
WAMR_BUILD_FAST_JIT 0     # asmjit 是 C++，已关（注释明言：保 vmlib 纯 C）
WAMR_BUILD_LAZY_JIT 0
WAMR_BUILD_FAST_INTERP 1
```

结论：**AOT 运行时已经在 libiwasm.a 里**（`deps/wamr/core/iwasm/aot/`：aot_loader.c / aot_runtime.c / aot_validator.c / aot_intrinsic.c / arch/）。当前缺的只是"把 .aot 字节喂进加载器"的 JS/C 层通路，以及生成 .aot 的宿主工具（wamrc）。

## 1.2 `wasm_runtime_load` 已按魔数自动分派（关键事实）

`deps/wamr/core/iwasm/common/wasm_runtime_common.c:1438` `wasm_runtime_load_ex()`：

- `get_package_type()`（:881）按前 4 字节判定：
  - `\0asm`（`0x6d736100`）→ `Wasm_Module_Bytecode` → `wasm_load()`（解释器）
  - `\0aot`（`AOT_MAGIC_NUMBER = 0x746f6100`，`core/config.h:86`）→ `Wasm_Module_AoT` → `aot_load_from_aot_file()`（AOT 加载器）
- 两个宏都开（qwrt 现状），**同一入口同时接受 .wasm 与 .aot 字节流，无需调用方选择格式**。
- AOT 加载失败分支：魔数不符 → "magic header not detected"；版本不符 → `aot_compatible_version()`（`aot_loader.c:4412`，严格 `== AOT_CURRENT_VERSION`，`config.h:87` = **5**）→ "unknown binary version"；目标架构/ABI/CPU features 不匹配 → sections 加载失败。全部返回 NULL + error_buf。

## 1.3 qwrt 的 WASM API 全部汇聚到 Module 构造器

`src/ext_wamr.c` 的入口拓扑：

```
WebAssembly.compile(bytes)              → JS_CallConstructor(Module, 1, argv)      (:419)
WebAssembly.instantiate(bytes, imports) → Module 构造器 → Instance 构造器           (:456)
WebAssembly.instantiate(Module, imports)→ Instance 构造器                            (:456)
compileStreaming / instantiateStreaming → JS 闭包 shim → WebAssembly.compile/instantiate (:527)
new WebAssembly.Module(bytes)           → wasm_runtime_load()                       (:580 wamr_module_constructor)
new WebAssembly.Instance(Module, obj)   → wasm_runtime_instantiate()                (:1318)
WebAssembly.validate(bytes)             → wasm_runtime_load()（直接）               (:401)
```

要点：

- **唯一插入点 = `wamr_module_constructor`（:580）**：改这一处即覆盖 compile / instantiate(bytes) / 两个 Streaming（shim 转发）。`wasm_runtime_load` 按魔数自动分派，.aot 字节本就通得过。
- **instance 层无差异**：`wasm_runtime_instantiate()`（:1318 调用）对 AOT module 内部走 aot 分支，调用方代码不变。
- **imports 双加载**：`wamr_register_module_imports()`（:907）在 load 后按声明的函数 import 注册原生符号并**重新 load 一次**（WAMR 在 load 期链接 import；`wasm_runtime_load` 原地改写输入缓冲，故需从原始字节重拷）。AOT 模块同样走 `aot_load_from_aot_file` + import 解析（`aot_resolve_symbols`），设计上兼容，但 qwrt 的双加载模式需在 .aot 字节上实跑验证（开放问题 O1）。
- `wamr_extract_buffer()`（:340）接受任意 ArrayBuffer / TypedArray —— .aot 字节直接可进。

## 1.4 wamrc（AOT 编译器）

- 位于 `deps/wamr/wamr-compiler/`，**依赖 LLVM**（`CMakeLists.txt:162-170`：`find_package(LLVM REQUIRED CONFIG)`，需 `core/deps/llvm/build`）。qwrt 的 `add_subdirectory(... EXCLUDE_FROM_ALL)` **不构建它**；本机无 LLVM 构建、无 wamrc 二进制（探查确认）。
- 调用：`wamrc [--target=<arch>] [--target-abi=<abi>] [--opt-level=0..3] -o out.aot in.wasm`（`wamr-compiler/main.c:112,132,221`）。
- 产出 .aot 绑死目标 arch/ABI，且版本必须等于运行期 WAMR 的 `AOT_CURRENT_VERSION`（qwrt 2.4.5 = 5）。

---

# 2. 约束

| # | 约束 | 依据 |
|---|------|------|
| C1 | vmlib 纯 C、无 C++ 运行时依赖 | 项目目标；FAST_JIT/LAZY_JIT 已因此关闭（CMakeLists:314 注释） |
| C2 | 调用方无感知回退；功能优先（fail-open） | 本任务目标 |
| C3 | AOT 只在 WAMR 路径；wasm3 / web-wasm 不动 | 多引擎共存 |
| C4 | 不内置 wamrc / 不引 LLVM 进 qwrt 构建 | C1 + wamrc 是 C++ 宿主工具 |
| C5 | 不改现有 JS 调用的兼容性（新增参数必须可省） | 快速回归面 |

---

# 3. 决策点 + 选择 + 理由

## D1 路径选择：何时走 AOT？

**候选 (a)**：调用方显式提供 .aot 字节（`WebAssembly.instantiate(bytes, importObject, {aot: aotBytes})`）。
**候选 (b)**：按文件扩展/约定路径自动探测外部 .aot（嵌入式宿主提供路径）。

**选择：(a)。** 理由：

1. **字节即事实**：qwrt 里模块一律以 `ArrayBuffer`/`TypedArray` 进入（fetch→`arrayBuffer()`、文件读入等），不存在"宿主文件系统路径"这一概念——CLI 无 wasm-from-file 通路（`src/cli.c` 探查无 .wasm/.aot 文件加载）。路径探测需要一个本不存在的 name→file 映射层，纯属新增宿主耦合。
2. **零成本基线**：`wasm_runtime_load` 已按魔数自动分派，调用方直接把 .aot 字节传给现有 API 就已走 AOT（**Tier 0，无需改代码，只补文档**）。`(b)` 反而要多写探测逻辑。
3. **保守默认**：无 .aot 显式提供 → 永不尝试 AOT，避免"文件在不在"这类隐式行为。

**落地形态——两层**：

- **Tier 0（现成能力，零代码）**：`new WebAssembly.Module(aotBytes)` / `WebAssembly.instantiate(aotBytes, imports)` 直接传 .aot 字节 → 自动走 AOT。仅需在文档中作为约定写明。
- **Tier 1（本设计主体）**：`new WebAssembly.Module(bytes, options?)`，`options.aot` 携带 .aot 字节，与可移植 .wasm 字节**并存**。运行期：有 `options.aot` 且 AOT 加载成功 → AOT；否则 → 解释器。调用方总是带 .wasm（保底）+ 可选 .aot（加速），**同一个模块对象**不因平台/版本变化而分叉行为。

## D2 回退语义：AOT 失败怎么办？

**选择：fail-open（静默回退解释器）+ 诊断日志。**

- 提供 `options.aot` 但 AOT 加载失败（版本 5 不符、arch/ABI 不符、损坏）→ 丢弃 AOT 结果，用 .wasm 字节走解释器；**不抛错**，调用方拿到的是可用的 Module/Instance。理由：功能优先；.aot 是"过期加速缓存"——wasm 源码更新后旧 .aot 不应把应用打挂；解释器路径保证语义正确（AOT 由同一 wasm 派生）。
- 诊断：qwrt 日志（debug/info 级）记录 AOT 失败原因（error_buf），**不**暴露为 JS 异常。可加 `options.aotFail` 回调（可选，默认无）供高级调用方探测"本次实际未用 AOT"。
- **边界**：调用方**只**给 .aot 字节（Tier 0）时无 .wasm 可回退 → 失败照旧抛 TypeError（保持现状语义，显式选择 = 显式失败）。
- **另一个边界**：若 `options.aot` 与 `bytes` 是同一来源（调用方误解 API），无影响——选择按"aot 优先"。

## D3 wamrc 集成：AOT 预编译放哪？

**选择：宿主离线预编译；qwrt 库不内置 wamrc，也不加构建期 CMake 目标。**

- wamrc 需 LLVM 构建（C++、GB 级），与 C1（vmlib 纯 C）+ C4 直接冲突；`EXCLUDE_FROM_ALL` 已是刻意隔离。
- 流程：开发者在**目标 arch/ABI** 的主机上用 wamrc 把 .wasm 编成 .aot（一次/发布版），与 .wasm 一起作为资源分发；JS 侧 `fetch` 两个资源后把 .aot 字节交给 `options.aot`。运行期加载 .aot 是纯 C（C1 满足）。
- 不在 qwrt 里生成 .aot：编译产物应随应用分发布局，而非运行库职责。测试期可用宿主 wamrc（若可用）产 .aot 做验证（见 §6）。
- AOT 文件按平台绑定：同 qwrt 运行平台编出的 .aot 才能被加载；跨平台（如 x86_64 编、arm 跑）→ D2 fail-open 回退解释器，不炸。

## D4 纯 C 约束确认

**结论：设计有效，vmlib 保持纯 C。**

- AOT **运行时**（加载器/执行器/校验器/intrinsics）全部是 C 文件（`core/iwasm/aot/*.c`），编译进 libiwasm.a。C++（LLVM）只存在于 **wamrc（宿主工具）**，不在 vmlib 里。
- `wasm_runtime_load_ex` 对 AOT 分支的调用是纯 C 符号（`aot_load_from_aot_file`）；qwrt 链接 vmlib 不需要 C++ 运行时（FAST_JIT/LAZY_JIT 已关，asmjit 不在）。
- 因此"解释器/AOT 双路径"与"纯 C 约束"**不冲突**——只有 wamrc（编译期）是 C++，运行期两侧都是 C。

## D5 与现有功能共存

- **compileStreaming / instantiateStreaming**：JS shim（:527）需透传第三个 options 参数给 compile/instantiate（小改两行字符串内 shim 代码）。语义仍"取全量字节再编译"，AOT 选项随 shim 透传。
- **imports（刚做的 importObject 支持）**：AOT 模块 + import → 走 `wamr_register_module_imports` 双加载模式（见 §1.3）。Module wrap 需记住"本次选的是 .aot 字节"，重加载时用同一份字节（Tier 1 里 wrap 加 `aot_buf` 字段或统一存"已选字节"）。**需实跑验证**（O1）。
- **validate**：直接 `wasm_runtime_load`，.aot 字节同样校验（magic 分派天然支持），无需改。
- **多引擎**：`QWRT_WITH_WAMR` 专属；wasm3（`ext_wasm3.c`，无 AOT 概念）与 web-wasm 分支不加 options 处理，保持现状。
- **Instance / 导出调用**：`wasm_runtime_instantiate` / `wasm_runtime_call_wasm` 对 AOT module 无调用方差异，instance wrap 结构不动。

## D6 接口：JS/C 两层

**JS 层（加法、可省）：**

```js
// Module 构造器：第二参数可选 options（非标准 qwrt 扩展，属性可省）
new WebAssembly.Module(wasmBytes, {
  aot: aotBytes,              // 可选；ArrayBuffer/TypedArray 的 .aot 字节
  aotFail: (err) => {...},    // 可选；AOT 加载失败回调（不含 .aot 时不被调）
});

// 既有 API 透传同一 options
WebAssembly.compile(wasmBytes, options);
WebAssembly.instantiate(wasmBytes, importObject, options);   // 第三参
WebAssembly.instantiateStreaming(src, importObject, options); // 第三参
WebAssembly.compileStreaming(src, options);                   // 第二参
```

- 缺省行为不变：无 options / 无 `options.aot` → 纯解释器（现状）。
- 优先级：`options.aot` 存在且 AOT load 成功 > 解释器。`bytes` 本身是 `\0aot`（Tier 0 直传）→ 也走 AOT（无回退，因无 .wasm）。

**C 层（pal 原语）：不需要新增。**

- 全部改动收敛在 `src/ext_wamr.c` 内部：`wamr_module_constructor` 里读 `options.aot` → 抽一个小 helper：
  `wamr_load_module(bytes, len, aot_bytes, aot_len, error_buf) → wasm_module_t`（内部：尝试 aot load，失败记录日志 + error_buf 后回退 wasm load）。
- `wamr_module_wrap_t` 增记"已选字节源"（aot 或 wasm）供 imports 双加载重载使用。
- 无新 public C API、无 pal 改动。

---

# 4. 建议实现落地（供实施参考，未实施）

1. `wamr_module_constructor`：解析 argv[1]（若为对象）取 `aot`/`aotFail`；拷贝 .aot 字节（同样需保持存活，同 wasm_buf 处理）。
2. 新增 `wamr_load_module(...)`：aot 优先 → 失败 `LOG` + 调 `aotFail` → 回退 wasm。
3. wrap 记录所选字节源；imports 双加载用同源字节重 load。
4. 两个 Streaming shim 字符串加 options 透传参数。
5. `validate` 无需改（自动分派已覆盖）。

---

# 5. 验收

1. **功能一致性**：宿主 wamrc 把示例 .wasm 编 .aot（同 arch/ABI、WAMR 2.4.5），`new WebAssembly.Module(wasmBytes, {aot})` → 实例化 → 导出函数结果与纯解释器**逐字节一致**。
2. **静默回退**：损坏 .aot / 伪造版本（改版本字段≠5）→ 仍拿到可用实例，导出可调用，日志出现 AOT 失败原因，不抛 JS 异常。
3. **性能**：CPU 密集 loop 的 wasm，AOT vs 解释器耗时对比（记录加速比；预期数量级提速）。
4. **imports**：带 importObject 的模块 + .aot → 导出可调、import 分发正确（O1 覆盖）。
5. **回归**：无 options 的既有调用（`instantiate(bytes, imports)` 等）行为完全不变。
6. **纯 C 验证**：构建 vmlib 无 C++ 链接（现状已满足，AOT 加载路径不引入新链接依赖）。

---

# 6. 可行性结论

- **AOT 加载通路：静态确认可用**。`wasm_runtime_load` 按魔数自动分派 `\0aot` → `aot_load_from_aot_file`，且 qwrt 已编入 AOT（`WAMR_BUILD_AOT 1`）。Tier 0（直传 .aot 字节）**理论上零改动即生效**。
- **.aot 生成（wamrc）：本机不可用，标注"可行性待验证"**。探查确认：无 LLVM 构建、无 wamrc 二进制、无 build 产物。要在本仓库跑出真 .aot 需先构建 wamrc 的 LLVM 依赖（`build_llvm.py`，C++/GB 级），超出本次只读范围。运行期加载 .aot 与纯 C 约束不冲突（D4），风险集中在"wamrc 产物能否被 2.4.5 加载器接受"——由 AOT_CURRENT_VERSION=5 + 同 arch/ABI 保证，需在具备 LLVM 的主机上做一次端到端验证后落定。

---

# 7. 开放问题

- **O1（需实跑）**：AOT 模块 + qwrt 的 imports 双加载模式是否完全兼容（native 注册 → 重载 .aot 字节 → `aot_resolve_symbols`）。设计按兼容处理，未运行验证。
- **O2**：AOT module 的 `wasm_runtime_instantiate` 参数（stack/mem 上限）与解释器行为是否逐项一致（大内存模块边界）。
- **O3**：`options.aot` 是否值得做成显式独立 API（如 `WebAssembly.Module.fromAot(aotBytes)`）替代隐式 options——本设计选 options（加法最小、可省）；若以后要严格 web 兼容边界，再评估独立命名空间。
- **O4**：Streaming shim 透传 options 后，`.aot` 与 `.wasm` 两个资源如何天然配对（fetch 两发 vs 单一响应含双字节）——留给调用方约定，运行库不介入。
- **O5**：`aotFail` 回调 API 形态是否保留（YAGNI 倾向：默认只日志；回调仅在确有人需要探测"未走 AOT"时加）。

---

# 附：探查依据（文件 + 行号）

- `CMakeLists.txt:309-320` — WAMR 构建宏（INTERP+AOT=1，JIT=0）
- `deps/wamr/core/iwasm/common/wasm_runtime_common.c:1438-1502` — `wasm_runtime_load_ex` 魔数分派
- `deps/wamr/core/iwasm/common/wasm_runtime_common.c:881-895` — `get_package_type`（`\0asm`/`\0aot`）
- `deps/wamr/core/config.h:86-87` — `AOT_MAGIC_NUMBER 0x746f6100`、`AOT_CURRENT_VERSION 5`
- `deps/wamr/core/iwasm/aot/aot_loader.c:4412-4444` — 版本/魔数校验
- `src/ext_wamr.c:580`（Module 构造器）、`:456`（instantiate）、`:527`（streaming shim）、`:907`（imports 双加载）、`:1318`（instantiate 调用）、`:401`（validate）、`:340`（extract_buffer）
- `deps/wamr/wamr-compiler/CMakeLists.txt:162-170` — wamrc 依赖 LLVM；`main.c:112,221` — CLI 用法
