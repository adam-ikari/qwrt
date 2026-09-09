# CI 修复 Backlog（nightly 2 点自动任务交接）

## 状态：✅ 全部完成（2026-09-09/10 nightly + 主会话连续修复）

CI 已全绿（run 34395551179 = ALL-GREEN，19 个 job 零失败）。
本 backlog 退出例行模式，转为归档记录。

## 已完成清单（按修复顺序）

| # | 提交 | 问题 | 根因与修复 |
|---|---|---|---|
| 1 | 75f3c47a | clang-tidy / ubsan(clang) Build | `qwrt_internal.h` typedef `qwrt_ctx_t` 重复（C11 特性，clang -Werror）→ struct 定义去尾部别名 |
| 2 | 75f3c47a | e2e — Service Worker | `test/sw-e2e/main*.js` 硬编码 `/home/gem/...` 本机路径 → `__SW_DIR__` 占位 + sed 注入 |
| 3 | 75f3c47a | test262 Not Run | quickjs-ng `EXCLUDE_FROM_ALL` → run-test262 不产出，ALL 依赖挂 target |
| 4 | fcf38813 | test262 执行失败 | 上游 test262 submodule `update=none` 阻断语料 → CI 显式拉取 |
| 5 | b4d9a290 | coverage gcov | RSA-OAEP 3072 keygen 同步阻塞 eval，gcov 下 5s 预算不足 → 30s |
| 6 | 115263d1 | ubsan(clang) fetch 5s 超时 | 见 #10（同根因不同面），预算 30s |
| 7 | 30ccb970 | ubsan(gcc) misaligned | WAMR loader 2/4 字节流嵌 8 字节指针（x86 有意未对齐）→ vmlib `-fno-sanitize=alignment` |
| 8 | 3dd786d3 | asan native stack overflow | GCC 13 ASan 默认 UAR 假栈，WAMR 栈边界检测误报 → `detect_stack_use_after_return=0` |
| 9 | bfabda0a | wasm3 gc_obj_list 断言 | ① 7 类补 `gc_mark` ② ext_destroy 过早清 class_id 致 gc_mark 失效（teardown 顺序 ctx→rt）③ memory import 明确报错 |
| 10 | 3a3a3e32 | ubsan(clang) fetch 死锁真根因 | sanitizer 帧放大 → QuickJS 默认 1MB JS 栈预算假溢出（fetch 同步链）→ `QWRT_SANITIZE_BUILD` 时 4MB |
| 11 | 3505dffc | ubsan(clang) function 误报 | quickjs js_realloc 函数指针类型链路 → clang sanitizer 档 `-fno-sanitize=function` |
| 12 | 3505dffc | ipc_envelope_fbcheck | CLI `EXCLUDE_FROM_ALL` → CI 干净构建缺失产物 → 移除 EXCLUDE |

## 基建

- lazy 初始化实施与评审修复：6c27d6a9 / 10bff38e（含 README 0a48f4b8）
- nightly 例行（2 点）：cron（supervisord 自愈）→ ci-nightly-once.sh → omp 无头会话按本文件推进；日志 /tmp/qwrt-ci-nightly/。brain 页 ci-nightly-repair 记录契约。

## 后续（非阻塞，可留档）

- CI 全绿后无例行任务；若未来 CI 再红，恢复本模式：gh run list → 定位 → 根因修复（禁放宽检查转绿）→ 提交 → 复验。