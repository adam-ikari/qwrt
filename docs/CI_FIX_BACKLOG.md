# CI 修复 Backlog（nightly 2 点自动任务交接）

例行任务：每晚 02:00 开始调查与修复 CI 失败，直到全绿。本文件由调度
`scripts/ci-nightly.sh` 拉起的 omp 无头会话读取，作为起点上下文。

## 已完成（勿重做）

- 09-09：低成本三项（commit 75f3c47a）——
  clang -Werror typedef 重定义（`src/qwrt_internal.h`）、SW e2e
  硬编码路径（`test/sw-e2e/main*.js` + 三脚本占位符注入）、test262
  runner EXCLUDE_FROM_ALL（顶层 ALL 依赖 target）。
- 09-09 nightly 首跑（commit fcf38813）：test262 语料显式拉取——上游
  quickjs-ng test262 submodule `update = none` 阻断 recursive checkout，
  test262 job 已绿。
- 修复后 CI 新绿：clang-tidy、e2e、minimal、ubsan(clang) Build、test262。

## 剩余（每次会话取一项，按序）

1. **coverage (gcov)**：`test_crypto_subtle_gtest.cpp:748` RSA-OAEP 3072
   setup eval failed（仅此 job 红，其余 18 job 同 SHA 绿）——gcov 插桩下
   mbedTLS 3072 密钥生成/解密超 host_eval 时限，间歇性。查 host_eval 超时
   与 coverage job 的 timeout 配置；方向：加大该测试超时或拆分 setup。
2. **ubsan (clang) Test**：`test_fetch_stream_gtest` 3 例 5s 超时
   （ResponseHasReadableStreamBody / TextReadsFullStreamingBody /
   RedirectManualStatusZero）——同 SHA 其余 job 全绿，UBSan 下 fetch 流式
   路径慢 5 倍或真 UB。本地 clang+ubsan 复现，区分超时 vs sanitizer 报告。
3. **ubsan (gcc)**：WAMR 上游 `deps/wamr/core/iwasm/interpreter/wasm_loader.c:9470`
   store to misaligned address（void*，需 8 对齐）——失败测试：
   test_wasm_streaming / test_wasm_imports / test_wasm_aot。方向：WAMR 本地
   patch（仓库已有 quickjs/wamr patch 先例，见 `deps/*.patch` 与 CMake
   apply 逻辑）对齐访问，或 CI 该 job 加 `-fno-sanitize=alignment` 豁免+
   理由注释。优先真修。
4. **asan (default WAMR)**：`WebAssembly function: Exception: native stack
   overflow`（test_wasm_streaming 两例，5s 超时）——WAMR native stack 边界
   在 ASan 帧放大下误判。方向：exec_env 创建处调 stack size /
   `wasm_runtime_set_native_stack_boundary`，或 WAMR 配置宏。
5. **wasm3**：`JS_FreeRuntime: Assertion list_empty(&gc_obj_list)` ×2
   （test_wasm_streaming/test_wasm_imports，wasm3 引擎配置）——wasm3 集成
   在 ctx 释放前未清 QuickJS GC 引用。方向：`src/ext_web_wasm.c` wasm3 路径
   对象释放。
6. （复验通过后）更新本文件与 BRAIN。

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
