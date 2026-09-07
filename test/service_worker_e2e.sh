#!/bin/bash
# SW-0 e2e — 单次 qwrt 运行覆盖 4 场景：激活状态、install→activate 顺序、
# postMessage 往返、同 URL 重注册（SW-3：字节未变 → 不触发 install、不替换）。
# 用法: bash test/service_worker_e2e.sh <path-to-qwrt>
set -u
QWRT="${1:-./build_grpc2/qwrt}"
DIR="$(cd "$(dirname "$0")/sw-e2e" && pwd)"

OUT="$(timeout 20 "$QWRT" "$DIR/main.js" 2>&1)"
EXPECTED='SW install
SW activate
p1: state=activated scope=/
p3: echo:ping
p4: old=activated new=activated cc=1
DONE'

if [ "$OUT" = "$EXPECTED" ]; then
  echo "PASS: service worker e2e — activation / event order / roundtrip / same-url no-reinstall"
  exit 0
fi
echo "FAIL: service worker e2e output mismatch"
diff <(echo "$EXPECTED") <(echo "$OUT")
exit 1
