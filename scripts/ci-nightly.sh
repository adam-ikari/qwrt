#!/bin/bash
# CI nightly 调度（02:00 本地时间）：无 cron/systemd 环境下由 hub 常驻进程
# 托管。每次触发拉起 omp 无头会话（--auto-approve），按 docs/CI_FIX_BACKLOG.md
# 推进一项修复；会话日志落 /tmp/qwrt-ci-nightly/nightly-<date>.log。
set -u
cd "$(dirname "$0")/.."
LOGDIR=/tmp/qwrt-ci-nightly
mkdir -p "$LOGDIR"

PROMPT='读取 docs/CI_FIX_BACKLOG.md，按"剩余"清单取第一项未完成项，完整执行：诊断根因、修复、本地验证（ON build_citest + ctest -L offline；涉及 sanitizer/引擎配置时用对应配置目录）、提交（中文 conventional commit）、push、gh run watch 复验并对比上次失败面。更新 backlog 已完成清单。一次会话只推进一项，做完即止。禁止为转绿放宽检查或删除测试。若清单全部完成，输出 CI-ALL-GREEN 并结束。'

while :; do
  now=$(date +%s)
  next=$(date -d 'tomorrow 02:00' +%s)
  if [ "$(date +%H:%M)" \< '02:00' ]; then next=$(date -d 'today 02:00' +%s); fi
  sleep $((next - now))
  log="$LOGDIR/nightly-$(date +%Y%m%d).log"
  echo "[$(date -Is)] nightly start" >> "$log"
  omp -p --auto-approve --max-time=45m "$PROMPT" >> "$log" 2>&1
  echo "[$(date -Is)] nightly end rc=$?" >> "$log"
done
