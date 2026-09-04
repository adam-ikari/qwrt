#!/bin/bash
# SW-1 e2e — fetch 拦截三场景：SW 合成响应（respondWith）/ SW 内 fetch
# 透传（防重入 + 真实网络回退 serve()）/ 未 respondWith 回退网络。
# 期望输出逐行匹配。用法: bash test/service_worker_sw1_e2e.sh <path-to-qwrt>
set -u
QWRT="${1:-./build_grpc2/qwrt}"
DIR="$(cd "$(dirname "$0")/sw-e2e" && pwd)"

OUT="$(timeout 20 "$QWRT" "$DIR/main-sw1.js" 2>&1)"
EXPECTED='SW1 install
SW1 activate
p1: 201 1 intercepted:GET
p2: 200 yes served:sw-passthrough
p3: 200 served:sw-plain
DONE'

if [ "$OUT" = "$EXPECTED" ]; then
  echo "PASS: service worker SW-1 e2e — intercept / passthrough / network-fallback"
  exit 0
fi
echo "FAIL: service worker SW-1 e2e output mismatch"
diff <(echo "$EXPECTED") <(echo "$OUT")
exit 1
