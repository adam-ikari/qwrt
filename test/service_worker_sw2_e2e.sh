#!/bin/bash
# SW-2 e2e — Cache API 集成：install pre-cache（addAll）命中 / 未命中网络
# 回填 put / 回填后缓存命中（serve 计数不涨）/ 放行请求证明计数。
# 期望输出逐行匹配。用法: bash test/service_worker_sw2_e2e.sh <path-to-qwrt>
set -u
QWRT="${1:-./build_grpc2/qwrt}"
DIR="$(cd "$(dirname "$0")/sw-e2e" && pwd)"

OUT="$(timeout 20 "$QWRT" "$DIR/main-sw2.js" 2>&1)"
EXPECTED='SW2 install
SW2 activate
p1: 200 served:1
p2: 200 served:2
p3: 200 served:2
p4: 200 served:3
DONE'

if [ "$OUT" = "$EXPECTED" ]; then
  echo "PASS: service worker SW-2 e2e — pre-cache / fill / cache-hit / network-count"
  exit 0
fi
echo "FAIL: service worker SW-2 e2e output mismatch"
diff <(echo "$EXPECTED") <(echo "$OUT")
exit 1
