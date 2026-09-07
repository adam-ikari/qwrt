#!/bin/bash
# SW-3 更新机制回归 e2e — 评审探针固化的两个场景：
# ①revert-during-install（C1）：A active → 装 Bslow（慢 install）→ 回滚 A 字节
#   → update() skip resolve、在途 B 被取消、controller 仍 A、B 不激活
# ②overlapping-activation（C2）：A→B（activate 门控）→C，B 激活中途被 supersede
#   → 无孤儿中间 worker（A、B 均 redundant）、最终 controller 是最后版本 C
# 脚本副本在 /tmp（mktemp）并以参数传给 main-sw3u.js（I3）——工作树零污染。
# 期望输出逐行匹配。用法: bash test/service_worker_sw3_update_e2e.sh <path-to-qwrt>
set -u
QWRT="${1:-./build_grpc2/qwrt}"
DIR="$(cd "$(dirname "$0")/sw-e2e" && pwd)"
TMP="$(mktemp --suffix=.js /tmp/qwrt-sw3u-run-XXXXXX)"
trap 'rm -f "$TMP"' EXIT
cp "$DIR/sw-sw3-a.js" "$TMP"
OUT="$(timeout 20 "$QWRT" "$DIR/main-sw3u.js" "$TMP" "$DIR" 2>&1)"
EXPECTED='SW3-A install
p0: same=true
SW3-A activate
p1: state=activated cc=1
SW3U-Bslow install
p2: skipResolve=true installingNull=true ctrlA=true
p3: during=A
p4: ctrlA=true during=A cc=1
SW3-B install
SW3-B activate
SW3U-C install
SW3U-C activate
p5: ctrlC=true during=C A=redundant B=redundant C=activated waitNull=true instNull=true
DONE'

if [ "$OUT" = "$EXPECTED" ]; then
  echo "PASS: service worker SW-3 update regression — revert-during-install / overlapping-activation"
  exit 0
fi
echo "FAIL: service worker SW-3 update regression output mismatch"
diff <(echo "$EXPECTED") <(echo "$OUT")
exit 1
