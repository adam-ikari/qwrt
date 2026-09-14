#!/bin/bash
# M-P2 host ↔ mainRT process split e2e (§11 M-P2 验证门 / §1.5 parity).
# Exercises the real two-process path against the qwrt CLI in an ISOLATED build:
#   1. eval round-trip: host qwrt_post_message → mainRT JS → message_cb → stdout
#   2. the split is real: while a script holds the runtime busy, a qwrt-rt child
#      exists under the host process (PID evidence, not inference)
#   3. clean exit: no leftover qwrt-rt child after a normal run
#   4. orphan reclamation (§6.4): SIGKILL the host → mainRT sees parent-fd EOF
#      → self-exits; no qwrt-rt process survives
#   5. worker-backend parity (§1.5): same worker script under THREAD and PROCESS
#      backends prints identical stdout
# Usage: bash test/test_mp2_host_split_e2e.sh <path-to-qwrt>
set -u
QWRT="${1:-./build/qwrt}"
DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -x "$QWRT" ]; then
  echo "FAIL: qwrt binary not found at '$QWRT'"
  exit 1
fi

fail() { echo "FAIL: $1"; [ -n "${2:-}" ] && { echo "--- got:"; printf '%s\n' "$2"; }; exit 1; }

# ── 1: eval round-trip (host → mainRT → host) ──
OUT="$(timeout 20 "$QWRT" -e 'console.log("mp2:" + (1 + 1))' 2>&1)"
[ "$OUT" = "mp2:2" ] || fail "1 eval round-trip mismatch" "$OUT"

# ── 3: no leftover child after a normal run ──
if pgrep -f "qwrt-rt" > /dev/null 2>&1; then
  fail "3 leftover qwrt-rt process after normal exit" "$(pgrep -af qwrt-rt)"
fi

# ── 2 + 4: real two-process split + orphan reclamation ──
TMP="$(mktemp)"
# 注意：此处不能包 timeout —— $HOST 必须是 qwrt 本身，否则 pgrep -P 找到的是
# timeout 的子进程而非主RT（mp1 e2e 同理）。清理靠下面的 kill -9。
"$QWRT" -e 'console.log("READY"); setInterval(function () {}, 50);' \
  > "$TMP" 2>&1 &
HOST=$!
for _ in $(seq 1 100); do
  grep -q '^READY$' "$TMP" 2>/dev/null && break
  sleep 0.1
done
grep -q '^READY$' "$TMP" || { kill "$HOST" 2>/dev/null; cat "$TMP"; rm -f "$TMP"; fail "2 mainRT never became ready"; }
CHILD="$(pgrep -P "$HOST" -f qwrt-rt | head -1)"
[ -n "$CHILD" ] || { kill "$HOST" 2>/dev/null; rm -f "$TMP"; fail "2 no qwrt-rt child under host $HOST (no process split)"; }

kill -9 "$HOST"                       # host dies → mainRT must self-exit (EOF)
wait "$HOST" 2>/dev/null
ALIVE=1
for _ in $(seq 1 100); do
  kill -0 "$CHILD" 2>/dev/null || { ALIVE=0; break; }
  sleep 0.1
done
rm -f "$TMP"
[ "$ALIVE" = 0 ] || { kill -9 "$CHILD" 2>/dev/null; fail "4 mainRT $CHILD survived host SIGKILL (orphan leak)"; }

# ── 5: dual-backend JS parity (§1.5) — same script, both worker backends ──
A="$(QWRT_WORKER_BACKEND=thread  timeout 30 "$QWRT" "$DIR/mp1-e2e/main-mp1.js" 2>&1)"
B="$(QWRT_WORKER_BACKEND=process timeout 30 "$QWRT" "$DIR/mp1-e2e/main-mp1.js" 2>&1)"
[ -n "$A" ] || fail "5 thread-backend run produced no output"
[ "$A" = "$B" ] || fail "5 dual-backend parity mismatch" "$(printf 'thread:\n%s\nprocess:\n%s' "$A" "$B")"

echo "PASS: M-P2 host/mainRT split e2e — eval round-trip / real two-process / clean exit / orphan reclaim / dual-backend parity"
