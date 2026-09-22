#!/bin/bash
# M-P3 跨进程 MessagePort e2e（§11 M-P3 验证门 / §8.2 peerEndpoint 路由）
# 真进程路径（AM_WORKER_BACKEND=process，缺省 ISOLATED 构建）：
#   1. 主RT↔worker 进程 port 往返——一端在主RT、一端在 worker 进程，转移后双向
#      收发；附 PID 证据（宿主/主RT/worker 三个不同进程，非推断）
#   2. sibling 接力：ch.port1→w1、ch.port2→w2，消息由主RT（二者 LCA）按帧头
#      dest 端点转发到 w2——§8.2 路由表语义
#   3. 崩溃清表：SIGKILL worker 进程 → 主RT 侧对端 port 收一次 error；死后
#      postMessage 静默不抛；主RT 存活并能 spawn 新 worker + 新 port 完成往返
#      （无 stale 路由命中新进程）
# Usage: bash test/test_mp3_port_e2e.sh <path-to-amoib>
set -u
AM="${1:-./build/amoib}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export AM_WORKER_BACKEND=process

FIX="$(mktemp -d)"
OUT=""
HOSTPID=""
cleanup() {
  [ -n "$HOSTPID" ] && kill -9 "$HOSTPID" 2>/dev/null
  rm -rf "$FIX"
}
trap cleanup EXIT

[ -x "$AM" ] || { echo "FAIL: amoib binary not found at '$AM'"; exit 1; }
fail() { echo "FAIL: $1"; [ -n "${2:-}" ] && { echo "--- got:"; printf '%s\n' "$2"; }; exit 1; }

# 进程后端可用性探针：THREAD 编译下 processSpawn 显式报错（不静默降级），本 e2e
# 只对 ISOLATED 构建有意义 → 跳过而非失败。
cat > "$FIX/probe.js" <<EOF
try { new Worker('file://$ROOT/test/worker_echo.js'); console.log('PROC-OK'); }
catch (e) { console.log('PROC-ERR:' + e.message); }
EOF
PROBE="$(timeout 15 "$AM" "$FIX/probe.js" 2>&1)"
case "$PROBE" in
  *PROC-OK*) ;;
  *) echo "SKIP: build has no process backend — $PROBE"; exit 0;;
esac

# ── Phase 2: sibling 接力（port 两端分属两个 worker 进程，经主RT 转发）──
cat > "$FIX/p2.js" <<EOF
var keep = setInterval(function () {}, 50);
var ch = new MessageChannel();
var w1 = new Worker('file://$ROOT/test/worker_port_sibling_send.js');
var w2 = new Worker('file://$ROOT/test/worker_port_sibling_recv.js');
w2.onmessage = function (e) {
  console.log('relay:' + e.data);
  if (String(e.data).indexOf('recv:sib-hello') === 0) {
    console.log('P2-DONE');
    w1.terminate(); w2.terminate();
    clearInterval(keep);
  }
};
w2.postMessage('init', [ch.port2]);
w1.postMessage('init', [ch.port1]);
EOF
OUT="$(timeout 30 "$AM" "$FIX/p2.js" 2>&1)" || fail "phase 2 exit" "$OUT"
printf '%s\n' "$OUT" | grep -q "P2-DONE" || fail "2 sibling relay across processes" "$OUT"

# ── Phase 1 + 3: 往返 + PID 证据 + 崩溃清表 ──
cat > "$FIX/p13.js" <<EOF
var keep = setInterval(function () {}, 50);
var ch = new MessageChannel();
ch.port2.onmessage = function (e) {
  console.log('port2:' + e.data);
  if (e.data === 'ready') ch.port2.postMessage('ping');
  if (String(e.data).indexOf('echo:') === 0) console.log('RT-DONE');
};
ch.port2.addEventListener('error', function (e) {
  console.log('port2-error:' + (e && e.message));
  try { ch.port2.postMessage('after-death'); console.log('post-after-death:silent'); }
  catch (ex) { console.log('post-after-death:throw'); }
  /* 死后新端口 + 新 worker：证明路由表已清理、不命中 stale 项 */
  var w2 = new Worker('file://$ROOT/test/worker_port.js');
  var ch2 = new MessageChannel();
  ch2.port2.onmessage = function (e2) {
    console.log('after:' + e2.data);
    if (e2.data === 'ready') ch2.port2.postMessage('ping');
    if (String(e2.data).indexOf('echo:') === 0) { console.log('AFTER-DONE'); clearInterval(keep); }
  };
  w2.postMessage('init', [ch2.port1]);
});
var w = new Worker('file://$ROOT/test/worker_port.js');
w.postMessage('init', [ch.port1]);
console.log('READY');
EOF

# 不能包 timeout：$HOSTPID 必须是 amoib 本身，否则 pgrep -P 找到的是 timeout 的子进程。
"$AM" "$FIX/p13.js" > "$FIX/p13.out" 2>&1 &
HOSTPID=$!

for _ in $(seq 1 100); do
  grep -q "RT-DONE" "$FIX/p13.out" 2>/dev/null && break
  kill -0 "$HOSTPID" 2>/dev/null || break
  sleep 0.1
done
OUT="$(cat "$FIX/p13.out" 2>/dev/null)"
printf '%s\n' "$OUT" | grep -q "port2:ready" || fail "1 port transfer to worker process" "$OUT"
printf '%s\n' "$OUT" | grep -q "port2:echo:ping" || fail "1 port round-trip mainRT<->worker" "$OUT"

# PID 证据：宿主 → 主RT（--amoib-rt-server）→ worker 进程，三者互不相同
MAINPID="$(pgrep -P "$HOSTPID" -f 'amoib-rt' 2>/dev/null | head -1)"
WKPID=""
[ -n "$MAINPID" ] && WKPID="$(pgrep -P "$MAINPID" 2>/dev/null | head -1)"
[ -n "$MAINPID" ] || fail "PID evidence: no mainRT child of host $HOSTPID" "$OUT"
[ -n "$WKPID" ] || fail "PID evidence: no worker child of mainRT $MAINPID" "$OUT"
[ "$MAINPID" != "$WKPID" ] && [ "$MAINPID" != "$HOSTPID" ] \
  || fail "PID evidence: not three distinct processes" "host=$HOSTPID main=$MAINPID worker=$WKPID"

# 崩溃清表：硬杀 worker 进程（非优雅 terminate）→ 主RT 经 fd EOF 感知并清表
kill -9 "$WKPID" 2>/dev/null || fail "kill -9 worker pid $WKPID failed" "$OUT"
for _ in $(seq 1 150); do
  grep -q "AFTER-DONE" "$FIX/p13.out" 2>/dev/null && break
  kill -0 "$HOSTPID" 2>/dev/null || break
  sleep 0.1
done
OUT="$(cat "$FIX/p13.out" 2>/dev/null)"
printf '%s\n' "$OUT" | grep -q "port2-error:" || fail "3 peer port error after worker crash" "$OUT"
printf '%s\n' "$OUT" | grep -q "post-after-death:silent" || fail "3 post after death must be silent" "$OUT"
printf '%s\n' "$OUT" | grep -q "after:echo:ping" || fail "3 no stale route: fresh port+worker works" "$OUT"

# 主RT/宿主在 worker 崩溃后仍完成了一次全新往返（上面 after:echo:ping 即证据），
# 随后脚本自行收束 → 等它干净退出（崩溃会体现为非零退出码）。
wait "$HOSTPID" 2>/dev/null || fail "3 host exited abnormally after worker crash" "$OUT"
HOSTPID=""

echo "PASS: M-P3 cross-process MessagePort e2e — port round-trip (3 distinct PIDs) / sibling relay via LCA / crash cleanup + no stale"
