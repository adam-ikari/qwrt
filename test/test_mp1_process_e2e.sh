#!/bin/bash
# M-P1 process-backend e2e (review I7) — exercises the real two-process path
# against the qwrt CLI (QWRT_WORKER_BACKEND=process). Two phases:
#   1. spawn → handshake → postMessage round-trip → graceful terminate (tier-1)
#   2. hard SIGKILL the child mid-flight → parent survives (C2 MSG_NOSIGNAL)
#      → zombie reaped + slot released (I1) so a fresh Worker spawns → no
#      leftover temp script file (C1)
# Usage: bash test/test_mp1_process_e2e.sh <path-to-qwrt>
set -u
QWRT="${1:-./build_e2e/qwrt}"
DIR="$(cd "$(dirname "$0")/mp1-e2e" && pwd)"
export QWRT_WORKER_BACKEND=process

if [ ! -x "$QWRT" ]; then
  echo "FAIL: qwrt binary not found at '$QWRT'"
  exit 1
fi

# Clean any stale temp files so the C1 leak check is meaningful.
rm -f /tmp/qwrt-worker-*

# ── Phase 1: graceful round-trip + terminate ──
OUT1="$(timeout 20 "$QWRT" "$DIR/main-mp1.js" 2>&1)"
EXP1=$'echo:ping\nDONE'
if [ "$OUT1" != "$EXP1" ]; then
  echo "FAIL: phase 1 (graceful round-trip) output mismatch"
  diff <(printf '%s\n' "$EXP1") <(printf '%s\n' "$OUT1")
  exit 1
fi

# ── Phase 2: hard kill + respawn + no zombie / no temp leak ──
TMP="$(mktemp)"
"$QWRT" "$DIR/main-mp1-kill.js" > "$TMP" 2>&1 &
PARENT=$!
# Wait for the worker to come up (READY) so the child exists to kill.
for i in $(seq 1 50); do
  grep -q '^READY$' "$TMP" 2>/dev/null && break
  sleep 0.1
done
CHILD="$(pgrep -P "$PARENT" -f qwrt-rt | head -1)"
if [ -z "$CHILD" ]; then
  echo "FAIL: phase 2 — no qwrt-rt child found under parent $PARENT"
  kill "$PARENT" 2>/dev/null
  wait "$PARENT" 2>/dev/null
  cat "$TMP"; rm -f "$TMP"
  exit 1
fi
kill -9 "$CHILD"      # tier-3 equivalent: unrecoverable death
wait "$PARENT"
RC=$?
OUT2="$(cat "$TMP")"; rm -f "$TMP"
EXP2=$'READY\nRESPAWNED\necho2:ping2\nDONE'
if [ "$OUT2" != "$EXP2" ]; then
  echo "FAIL: phase 2 (hard kill + respawn) output mismatch (rc=$RC)"
  diff <(printf '%s\n' "$EXP2") <(printf '%s\n' "$OUT2")
  exit 1
fi

# Zombie check (I1): the killed child must have been reaped — no qwrt-rt lingers.
if pgrep -f qwrt-rt >/dev/null; then
  echo "FAIL: phase 2 — leftover qwrt-rt process (zombie not reaped)"
  pgrep -af qwrt-rt
  exit 1
fi

# Temp-file leak check (C1): the child unlinks its script after reading.
if ls /tmp/qwrt-worker-* >/dev/null 2>&1; then
  echo "FAIL: temp script files leaked:"
  ls /tmp/qwrt-worker-*
  exit 1
fi

echo "PASS: M-P1 process-backend e2e — round-trip / graceful-terminate / hard-kill-reap / no-temp-leak"
exit 0
