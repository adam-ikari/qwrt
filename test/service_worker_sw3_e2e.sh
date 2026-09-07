#!/bin/bash
# SW-3 e2e — 更新机制三验证门：①同 URL 字节未变不触发 install
# ②字节变化 → install→activate→替换 ③新 SW install 期间旧 SW 仍拦截（无控制真空）。
# 脚本副本放在 /tmp（mktemp）并以参数传给 main-sw3.js（I3）——对工作树零污染、
# 并行安全。期望输出逐行匹配。用法: bash test/service_worker_sw3_e2e.sh <path-to-qwrt>
set -u
QWRT="${1:-./build_grpc2/qwrt}"
DIR="$(cd "$(dirname "$0")/sw-e2e" && pwd)"
TMP="$(mktemp --suffix=.js /tmp/qwrt-sw3-run-XXXXXX)"
trap 'rm -f "$TMP"' EXIT
cp "$DIR/sw-sw3-a.js" "$TMP"

OUT="$(timeout 20 "$QWRT" "$DIR/main-sw3.js" "$TMP" "$DIR" 2>&1)"
EXPECTED='SW3-A install
SW3-A activate
p1: state=activated cc=1
p2: sameReg=true sameCtrl=true cc=1
SW3-B install
SW3-B activate
p3: during=A ctrlA=true
p4: after=B ctrlB=true cc=2
p5: sameReg=true sameCtrl=true cc=2
DONE'

if [ "$OUT" = "$EXPECTED" ]; then
  echo "PASS: service worker SW-3 e2e — byte-compare skip / update-replace / no-control-vacuum"
  exit 0
fi
echo "FAIL: service worker SW-3 e2e output mismatch"
diff <(echo "$EXPECTED") <(echo "$OUT")
exit 1
