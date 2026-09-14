#!/bin/bash
# M-P4 e2e — 单所有者 storage 代理（§10.2）+ 崩溃恢复/孤儿回收（§9.3/§9.4）
# 真进程路径（缺省 ISOLATED 构建，QWRT_WORKER_BACKEND=process）：
#   1. 跨进程 storage：worker 内 localStorage.* 经 kind=STORAGE 信封同步 RPC 到
#      主RT 所有者执行——跨进程一致（worker 写主RT 读、主RT 写 worker 读）、
#      同步 API 语义（含 QuotaExceededError 异常形状）、持久化落盘
#   2. worker 崩溃（kill -9）→ 主RT 收 fd EOF → dispatch onerror（§9.3）+ 主RT
#      继续存活并 spawn 新 worker；子进程被收尸（无 zombie）
#   3. worker 自愿 close() → 静默（不误报 onerror）
#   4. 主RT 崩溃（kill -9）→ 宿主 message_cb 收 {type:'error'}（§9.3）+ 退出码非零
#      + worker 连锁自杀（§9.4，无残留）
#   5. 宿主被杀 → 主RT + worker 自杀（§6.4/§9.4 孤儿回收，无泄漏进程）
#   6. 洪泛：2000 条跨进程往返无丢失（计数 + 校验和精确），进程干净退出
# Usage: bash test/test_mp4_storage_crash_e2e.sh <path-to-qwrt>
set -u
QWRT="${1:-./build/qwrt}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"
FIX="$(mktemp -d)"
TMPLS="$(mktemp -d)"
export QWRT_WORKER_BACKEND=process
HOSTPID=""

cleanup() {
  [ -n "$HOSTPID" ] && kill -9 "$HOSTPID" 2>/dev/null
  rm -rf "$FIX" "$TMPLS"
}
trap cleanup EXIT

[ -x "$QWRT" ] || { echo "FAIL: qwrt binary not found at '$QWRT'"; exit 1; }
fail() { echo "FAIL: $1"; [ -n "${2:-}" ] && { echo "--- got:"; printf '%s\n' "$2"; }; exit 1; }

# fixtures 用 __ROOT__ 占位（repo 根运行时替换，CI 可移植）
for f in "$DIR"/mp4-e2e/*.js; do
  sed "s#__ROOT__#$ROOT#g" "$f" > "$FIX/$(basename "$f")"
done

# 进程后端可用性探针（THREAD 编译下 processSpawn 显式报错，不静默降级）→ 跳过
cat > "$FIX/probe.js" <<EOF
try { new Worker('file://$ROOT/test/mp4-e2e/worker_echo.js'); console.log('PROC-OK'); }
catch (e) { console.log('PROC-ERR:' + e.message); }
EOF
PROBE="$(timeout 15 "$QWRT" "$FIX/probe.js" 2>&1)"
case "$PROBE" in
  *PROC-OK*) ;;
  *) echo "SKIP: build has no process backend — $PROBE"; exit 0;;
esac

# ── 1: 跨进程 storage（worker 代理 → 主RT 所有者，§10.2）──
export QWRT_LOCALSTORAGE_FILE="$TMPLS/ls.json"
OUT="$(timeout 30 "$QWRT" "$FIX/main_storage.js" 2>&1)" || fail "1 storage run"
printf '%s\n' "$OUT" | grep -q "xproc:get=v1"        || fail "1 worker reads mainRT value" "$OUT"
printf '%s\n' "$OUT" | grep -q "|len=2|"            || fail "1 worker length" "$OUT"
printf '%s\n' "$OUT" | grep -q "|key0=k1|"          || fail "1 worker key(n)" "$OUT"
printf '%s\n' "$OUT" | grep -q "|missing=null|"     || fail "1 missing key → null" "$OUT"
printf '%s\n' "$OUT" | grep -q "|quota=QuotaExceededError|" || fail "1 quota DOMException across processes" "$OUT"
printf '%s\n' "$OUT" | grep -q "|after-remove=null" || fail "1 removeItem" "$OUT"
printf '%s\n' "$OUT" | grep -q "main-sees:wval"     || fail "1 mainRT reads worker write (cross-process consistency)" "$OUT"
printf '%s\n' "$OUT" | grep -q "main-len:1"         || fail "1 mainRT length after worker remove" "$OUT"
printf '%s\n' "$OUT" | grep -q "main-removed:null"  || fail "1 mainRT sees worker removeItem" "$OUT"
printf '%s\n' "$OUT" | grep -q "STORAGE-DONE"       || fail "1 done" "$OUT"
grep -q '"wkey":"wval"' "$TMPLS/ls.json" 2>/dev/null || fail "1 owner persisted worker write" "$(cat "$TMPLS/ls.json" 2>/dev/null)"

# ── 2: worker 崩溃注入（kill -9）→ 主RT onerror + 继续 + 收尸 ──
"$QWRT" "$FIX/main_worker_crash.js" > "$FIX/crash.out" 2>&1 &
HOSTPID=$!
for _ in $(seq 1 100); do
  grep -q "WORKER-READY" "$FIX/crash.out" 2>/dev/null && break
  kill -0 "$HOSTPID" 2>/dev/null || break
  sleep 0.1
done
MAINPID="$(pgrep -P "$HOSTPID" -f 'qwrt-rt' 2>/dev/null | head -1)"
[ -n "$MAINPID" ] || fail "2 no mainRT child of host $HOSTPID" "$(cat "$FIX/crash.out")"
WKPID="$(pgrep -P "$MAINPID" 2>/dev/null | head -1)"
[ -n "$WKPID" ] || fail "2 no worker child of mainRT $MAINPID" "$(cat "$FIX/crash.out")"
kill -9 "$WKPID" 2>/dev/null || fail "2 kill -9 worker $WKPID failed"
for _ in $(seq 1 150); do
  grep -q "CRASH-DONE" "$FIX/crash.out" 2>/dev/null && break
  kill -0 "$HOSTPID" 2>/dev/null || break
  sleep 0.1
done
OUT="$(cat "$FIX/crash.out" 2>/dev/null)"
printf '%s\n' "$OUT" | grep -q "ONERROR:Worker process exited unexpectedly" || fail "2 mainRT onerror after worker crash (§9.3)" "$OUT"
printf '%s\n' "$OUT" | grep -q "after-crash:echo:ping" || fail "2 fresh worker works after crash" "$OUT"
printf '%s\n' "$OUT" | grep -q "CRASH-DONE" || fail "2 crash phase done" "$OUT"
# 收尸（无 zombie）：被 SIGKILL 的 worker 必须已被父 waitpid 回收
for _ in $(seq 1 50); do kill -0 "$WKPID" 2>/dev/null || break; sleep 0.1; done
kill -0 "$WKPID" 2>/dev/null && fail "2 zombie: worker $WKPID still present (not reaped)"
wait "$HOSTPID" 2>/dev/null || fail "2 host exited abnormally after worker crash" "$OUT"
HOSTPID=""

# ── 3: worker 自愿 close() → 静默（不自报崩溃）──
OUT="$(timeout 20 "$QWRT" "$FIX/main_selfclose.js" 2>&1)" || fail "3 selfclose run" "$OUT"
printf '%s\n' "$OUT" | grep -q "WORKER-READY" || fail "3 worker ready" "$OUT"
printf '%s\n' "$OUT" | grep -q "SELFCLOSE-DONE" || fail "3 selfclose done" "$OUT"
printf '%s\n' "$OUT" | grep -q "SPURIOUS-ONERROR" && fail "3 self-close must not fire onerror" "$OUT"

# ── 4: 主RT 崩溃（kill -9）→ 宿主 message_cb error（§9.3）+ worker 连锁自杀 ──
"$QWRT" "$FIX/main_mainrt_hold.js" > "$FIX/rt.out" 2>&1 &
HOSTPID=$!
for _ in $(seq 1 100); do
  grep -q "WORKER-READY" "$FIX/rt.out" 2>/dev/null && break
  kill -0 "$HOSTPID" 2>/dev/null || break
  sleep 0.1
done
MAINPID="$(pgrep -P "$HOSTPID" -f 'qwrt-rt' 2>/dev/null | head -1)"
WKPID="$(pgrep -P "$MAINPID" 2>/dev/null | head -1)"
[ -n "$MAINPID" ] && [ -n "$WKPID" ] || fail "4 PID evidence (main=$MAINPID worker=$WKPID)"
kill -9 "$MAINPID" 2>/dev/null || fail "4 kill -9 mainRT $MAINPID failed"
wait "$HOSTPID"; RC=$?
HOSTPID=""
OUT="$(cat "$FIX/rt.out" 2>/dev/null)"
printf '%s\n' "$OUT" | grep -q "main-runtime-process-exited-unexpectedly" || fail "4 host message_cb error on mainRT crash (§9.3)" "$OUT"
[ "$RC" -ne 0 ] || fail "4 host exit code must be non-zero on mainRT crash (got $RC)" "$OUT"
# 连锁死亡（§9.4）：worker 的 parent-fd 随主RT 终结 → 自杀；两者都不残留
for _ in $(seq 1 50); do
  kill -0 "$MAINPID" 2>/dev/null || { kill -0 "$WKPID" 2>/dev/null || break; }
  sleep 0.1
done
kill -0 "$WKPID" 2>/dev/null && fail "4 orphan worker $WKPID survived mainRT death (chain death §9.4)"
kill -0 "$MAINPID" 2>/dev/null && fail "4 killed mainRT $MAINPID still present (zombie?)"

# ── 5: 宿主被杀 → 主RT + worker 自杀（§6.4/§9.4 孤儿回收）──
"$QWRT" "$FIX/main_mainrt_hold.js" > "$FIX/orph.out" 2>&1 &
HOSTPID=$!
for _ in $(seq 1 100); do
  grep -q "WORKER-READY" "$FIX/orph.out" 2>/dev/null && break
  kill -0 "$HOSTPID" 2>/dev/null || break
  sleep 0.1
done
MAINPID="$(pgrep -P "$HOSTPID" -f 'qwrt-rt' 2>/dev/null | head -1)"
WKPID="$(pgrep -P "$MAINPID" 2>/dev/null | head -1)"
[ -n "$MAINPID" ] && [ -n "$WKPID" ] || fail "5 PID evidence (main=$MAINPID worker=$WKPID)"
kill -9 "$HOSTPID" 2>/dev/null || fail "5 kill -9 host $HOSTPID failed"
HOSTPID=""
for _ in $(seq 1 100); do kill -0 "$MAINPID" 2>/dev/null || break; sleep 0.1; done
kill -0 "$MAINPID" 2>/dev/null && fail "5 orphan mainRT $MAINPID survived host death (§6.4)"
for _ in $(seq 1 100); do kill -0 "$WKPID" 2>/dev/null || break; sleep 0.1; done
kill -0 "$WKPID" 2>/dev/null && fail "5 orphan worker $WKPID survived host death (§9.4)"

# ── 6: 洪泛 2000 条跨进程往返，无丢失（计数 + 校验和精确）──
cat > "$FIX/flood.js" <<EOF
var keep = setInterval(function () {}, 50);
var w = new Worker('file://$ROOT/test/mp4-e2e/worker_echo.js');
var N = 2000, got = 0, sum = 0;
w.onmessage = function (e) {
  var m = String(e.data);
  if (m.indexOf('echo:') === 0) { got++; sum += Number(m.slice(5)); }
  if (got === N) { console.log('FLOOD:' + got + ':' + sum); clearInterval(keep); }
};
for (var i = 1; i <= N; i++) w.postMessage(String(i));
EOF
OUT="$(timeout 90 "$QWRT" "$FIX/flood.js" 2>&1)" || fail "6 flood run" "$OUT"
printf '%s\n' "$OUT" | grep -q "FLOOD:2000:2001000" || fail "6 flood lossless (2000 × sum 2001000)" "$OUT"

echo "PASS: M-P4 storage single-owner proxy (§10.2) + crash recovery (worker kill -9 onerror / self-close silent / mainRT kill host error + chain death / host kill orphan reclaim) + 2000-msg flood lossless"
