# CI 修复 Backlog（nightly 2 点自动任务交接）

例行任务：每晚 02:00 开始调查与修复 CI 失败，直到全绿。本文件由调度
`scripts/ci-nightly.sh` 拉起的 omp 无头会话读取，作为起点上下文。

## 已完成（勿重做）

- 09-09：低成本三项（commit 75f3c47a）——
  clang -Werror typedef 重定义（`src/qwrt_internal.h`）、SW e2e
  硬编码路径（`test/sw-e2e/main*.js` + 三脚本占位符注入）、test262
  runner EXCLUDE_FROM_ALL（顶层 ALL 依赖 target）。
- 修复后 CI（run 34338481085）新绿：clang-tidy、e2e、minimal 配置。

## 剩余（每次会话取一项，按序）

1. **test262（新暴露）**：runner 现已产出但执行 Failed 0.00s——查
   `ctest -L test262 --output-on-failure` 本地复现（build_citest），
   根因候选：test262/test 子模块目录缺失（checkout recursive 是否拉全、
   .gitmodules）、conf 路径。CI 日志：`gh run view 34338481085 --log-failed`。
2. **ubsan (gcc)**：WAMR 上游 `deps/wamr/core/iwasm/interpreter/wasm_loader.c:9470`
   store to misaligned address（void*，需 8 对齐）——失败测试：
   test_wasm_streaming / test_wasm_imports / test_wasm_aot。方向：WAMR 本地
   patch（仓库已有 quickjs/wamr patch 先例，见 `deps/*.patch` 与 CMake
   apply 逻辑）对齐访问，或 CI 该 job 加 `-fno-sanitize=alignment` 豁免+
   理由注释。优先真修。
3. **asan (default WAMR)**：`WebAssembly function: Exception: native stack
   overflow`（test_wasm_streaming 两例，5s 超时）——WAMR native stack 边界
   在 ASan 帧放大下误判。方向：exec_env 创建处调 stack size /
   `wasm_runtime_set_native_stack_boundary`，或 WAMR 配置宏。
4. **wasm3**：`JS_FreeRuntime: Assertion list_empty(&gc_obj_list)` ×2
   （test_wasm_streaming/test_wasm_imports，wasm3 引擎配置）——wasm3 集成
   在 ctx 释放前未清 QuickJS GC 引用。方向：`src/ext_web_wasm.c` wasm3 路径
   对象释放。
5. （复验通过后）更新本文件与 BRAIN。

## 操作规约

- 诊断：`gh run list --limit 5`；`gh run view <id> --log-failed`。
- 本地：ON 配置 `build_citest`；OFF `build_minoff`；改动后先跑
  `ctest -L offline`。禁止为转绿而放宽 sanitizer/删除测试——修复必须对症。
- 提交：中文 conventional commit；push master；`gh run watch` 复验并对比
  上一次失败面（comm -23）。
- 一次会话完成一项即止（防上下文溢出）；多项时按序取第一未完成项。

## 调度持久化（2026-09-09 补）

- 容器无 systemd PID1，init = supervisord（/opt/gem/supervisord.conf）。
- cron 自愈已入 supervisord：/opt/gem/supervisord/cron.conf +
  /opt/gem/start-cron.sh（幂等：已有 cron 守护则 sleep infinity，避免
  crond.pid 锁冲突）——容器重启后 cron 自动拉起，crontab 条目随之生效。
- crontab 条目：`0 2 * * * /home/gem/project/qwrt/scripts/ci-nightly-once.sh`。
- 无需手动恢复；会话期 hub 常驻调度已弃用。
