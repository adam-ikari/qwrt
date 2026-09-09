#!/bin/bash
# CI nightly（02:00 cron 触发）：拉起 omp 无头会话，按 docs/CI_FIX_BACKLOG.md
# 推进一项 CI 修复。日志 /tmp/qwrt-ci-nightly/。
export PATH=/home/gem/.bun/bin:/home/gem/.local/bin:/home/gem/.local/share/pnpm:/usr/local/bin:/usr/bin:/bin
export HOME=/home/gem
LOGDIR=/tmp/qwrt-ci-nightly
mkdir -p "$LOGDIR"
cd /home/gem/project/qwrt || exit 1
log="$LOGDIR/nightly-$(date +%Y%m%d).log"
echo "[$(date -Is)] nightly start" >> "$log"
/home/gem/.bun/bin/omp -p --auto-approve --max-time=45m \
  '读取 docs/CI_FIX_BACKLOG.md，按"剩余"清单取第一项未完成项，完整执行：诊断根因、修复、本地验证（ON build_citest + ctest -L offline；涉及 sanitizer/引擎配置时用对应配置目录）、提交（中文 conventional commit）、push、gh run watch 复验并对比上次失败面。更新 backlog 已完成清单。一次会话只推进一项，做完即止。禁止为转绿放宽检查或删除测试。若清单全部完成，输出 CI-ALL-GREEN 并结束。' \
  >> "$log" 2>&1
echo "[$(date -Is)] nightly end rc=$?" >> "$log"
