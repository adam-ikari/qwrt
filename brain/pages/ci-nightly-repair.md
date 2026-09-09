---
id: ci-nightly-repair
title: "CI 修复例行：每晚 02:00 自动调查修复"
category: decision
status: active
tags: [ci, nightly, scheduling]
created: "2026-09-09T10:34:20"
updated: "2026-09-09T12:00:18"
---

<!-- compiled_truth -->
## 决策

用户持久需求：**每天晚上 02:00 开始调查和修复 CI/CD 问题**（反复确认 ≥5 次，视为例行契约，非一次性任务）。

## 落地机制（2026-09-09）

- **调度**：系统 cron（容器无 systemd PID1，cron 由 supervisord 托管自愈——`/opt/gem/supervisord/cron.conf` + 幂等 `start-cron.sh`；容器重启后 cron 自动拉起）。
- **crontab**：`0 2 * * * /home/gem/project/qwrt/scripts/ci-nightly-once.sh`。
- **执行**：omp 无头会话（`-p --auto-approve --max-time=45m`）按 `docs/CI_FIX_BACKLOG.md` 剩余清单取第一项未完成项，完整执行 诊断→修复→本地验证→提交→push→CI 复验；一次一项；禁止为转绿放宽检查/删测试。
- **日志**：`/tmp/qwrt-ci-nightly/nightly-<date>.log`。
- **终止**：backlog 全部完成时输出 CI-ALL-GREEN。

## 状态（2026-09-09 第二跑后）

已转绿：clang-tidy、e2e、minimal、test262、ubsan(clang) Build、**coverage (gcov)**。

剩余红（4 项，nightly 逐项，权威清单见 `docs/CI_FIX_BACKLOG.md`）：
1. ubsan (clang) Test — fetch 流式 3 例超时
2. ubsan (gcc) — WAMR `wasm_loader.c:9470` misaligned store
3. asan (default WAMR) — native stack overflow 误判
4. wasm3 — `JS_FreeRuntime` gc_obj_list 断言

## 可复用的诊断模式：sanitizer/coverage 档超时失败

CI 只在 coverage/asan/ubsan 档红、同 SHA 其余 job 绿时，先分清两件事：

- **真 sanitizer 报告**（日志含 `runtime error:` / `ERROR: AddressSanitizer`）→ 对症修代码。
- **harness 超时预算不匹配**→ gcov/`-O0`/sanitizer 插桩让重计算（RSA keygen、大 buffer 密码学）慢 3-5 倍，而 `test_host.h` 的 `host_eval` 默认 `timeout_ms = 5000`。

关键陷阱：polyfill `crypto-subtle.js` 的 RSA `generateKey` 在 `try {}` 内**同步**调 `pal.nativeRsaGenerateKey`，全部耗时计入 eval 的同步段。此时测试里 `host_poll_until_value(..., 30000)` 的宽预算**无效**——它只覆盖异步段。症状是 setup eval 那行 `FAIL() << "... eval failed"`，而非轮询超时。修法：给该 setup `host_eval` 显式传与 poll 对齐的预算，并在注释写明测得的真实耗时。

coverage 档本地验证需独立配置目录（CI 用 `-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="--coverage -fprofile-update=atomic -g -O0"`）。注意 `ipc_envelope_fbcheck` 依赖 `EXCLUDE_FROM_ALL` 的 `ipc_envelope_cli`，全新 coverage 目录跑 `ctest -L offline` 前需先单独 build 该 target（否则报 FileNotFoundError，与本项无关）。


## Timeline

- time: 2026-09-09T10:34:20
  kind: decision
  summary: "Created this page: CI 修复例行：每晚 02:00 自动调查修复"
  source: "用户指令 2026-09-09"
  affects: [ci-nightly-repair]

- time: 2026-09-09T10:34:20
  kind: decision
  summary: "用户持久需求：每晚 02:00 开始调查和修复 CI/CD 问题，直至全绿"
  source: brain update-truth
  affects: [ci-nightly-repair]

- time: 2026-09-09T12:00:18
  kind: decision
  summary: "第二跑：coverage (gcov) 转绿（b4d9a290，3072 keygen setup eval 5s→30s）；补录 sanitizer 档超时 vs 真 UB 的判别模式"
  source: "nightly 会话 2026-09-09 第二跑"
  affects: [ci-nightly-repair]
