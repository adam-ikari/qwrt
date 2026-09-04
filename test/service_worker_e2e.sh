#!/bin/bash
# SW-0 e2e — 单次 qwrt 运行覆盖 4 场景：激活状态、install→activate 顺序、
# postMessage 往返、同 URL 重注册替换。期望输出逐行匹配。
# 用法: bash test/service_worker_e2e.sh <path-to-qwrt>
set -u
QWRT="${1:-./build_grpc2/qwrt}"
DIR="$(cd "$(dirname "$0")/sw-e2e" && pwd)"

OUT="$(timeout 20 "$QWRT" "$DIR/main.js" 2>&1)"
EXPECTED='SW install
SW activate
p1: state=activated scope=/
p3: echo:ping
SW install
SW activate
p4: old=redundant new=activated cc=2
DONE'

if [ "$OUT" = "$EXPECTED" ]; then
  echo "PASS: service worker e2e — activation / event order / roundtrip / update-replace"
  exit 0
fi
echo "FAIL: service worker e2e output mismatch"
diff <(echo "$EXPECTED") <(echo "$OUT")
exit 1
