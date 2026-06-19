#!/usr/bin/env bash
# start-webui.sh — launch the dashboard on :1000, pinned to the non-SUT cores (accept-bench-ch)
# so it never contends for the cores being measured. Idempotent: restarts if already running.
set -uo pipefail
cd "$(dirname "$0")/.."
. harness/config
PORT="${WEBUI_PORT:-1000}"
mkdir -p "$RESULTS_DIR/loop-logs"
OUT="$RESULTS_DIR/loop-logs/webui.out"
pkill -f 'webui/server.py' 2>/dev/null; sleep 0.3
if [ -d /sys/fs/cgroup/accept-bench-ch ]; then
  WEBUI_PORT="$PORT" setsid harness/cgrun.sh accept-bench-ch python3 webui/server.py >"$OUT" 2>&1 < /dev/null &
else
  WEBUI_PORT="$PORT" setsid python3 webui/server.py >"$OUT" 2>&1 < /dev/null &
fi
sleep 1
if curl -fsS "http://localhost:$PORT/api/state" >/dev/null 2>&1; then
  ip=$(hostname -I 2>/dev/null | awk '{print $1}')
  echo "dashboard up:  http://${ip:-localhost}:$PORT   (local: http://localhost:$PORT)"
  echo "  log: $OUT"
else
  echo "webui failed to start — see $OUT"; tail -5 "$OUT" 2>/dev/null
fi
