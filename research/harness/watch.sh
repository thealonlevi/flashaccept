#!/usr/bin/env bash
# watch.sh — live dashboard for the optimizer loop. Read-only; safe to run anytime, anywhere
# (no claude, no measurement interference — it only reads files + queries ClickHouse).
#   harness/watch.sh            # one snapshot
#   harness/watch.sh --follow   # refresh every 5s (Ctrl-C to quit)
set -uo pipefail
cd "$(dirname "$0")/.."
. harness/config
LOGOUT="$RESULTS_DIR/loop-logs/loop.out"
ITERCSV="$RESULTS_DIR/loop-logs/iterations.csv"
INT="${WATCH_INTERVAL:-5}"
C0='\033[0m'; CB='\033[1m'; CG='\033[32m'; CY='\033[33m'; CC='\033[36m'; CR='\033[31m'

snapshot(){
  printf "${CB}${CC}══ accept-bench optimizer ══${C0}  %s\n" "$(date '+%F %T')"

  # --- loop process + current phase ---
  local pid; pid="$(pgrep -f 'harness/loop.sh' | head -1 || true)"
  if [ -n "$pid" ]; then
    local lastline; lastline="$(tail -1 "$LOGOUT" 2>/dev/null)"
    local phase="ANALYTICS (claude/think)"; echo "$lastline" | grep -qiE 'ramp|step|measur' && phase="MEASURE (ramp)"
    printf "  status: ${CG}RUNNING${C0} pid=%s   phase≈%s\n" "$pid" "$phase"
    grep -oE 'iteration [0-9]+ \(no_improve=[0-9]+/[0-9]+\)' "$LOGOUT" 2>/dev/null | tail -1 | sed 's/^/  /'
  else
    printf "  status: ${CY}not running${C0}   (start: harness/start-loop.sh)\n"
  fi

  # --- champion ---
  if [ -f "$BEST" ]; then
    python3 -c "import json;d=json.load(open('$BEST'));print('  champion: score=%s  cfg=%s  note=%s'%(d.get('score'),(str(d.get('config_hash'))[:10]),d.get('note','')))" 2>/dev/null
  fi
  [ -f "$RESULTS_DIR/CONTROL_BASELINE.json" ] && python3 -c "import json;print('  control baseline: score=%s'%json.load(open('$RESULTS_DIR/CONTROL_BASELINE.json')).get('score'))" 2>/dev/null

  # --- iteration trajectory (local CSV; instant, no CH needed) ---
  if [ -f "$ITERCSV" ] && [ "$(wc -l < "$ITERCSV")" -gt 1 ]; then
    printf "${CB}  iter   score   champion  verdict           ceiling          cost   cum\$${C0}\n"
    tail -8 "$ITERCSV" | awk -F, 'NR>0{
      v=$6; c=($6=="promote")?"\033[32m"$6"\033[0m":(($6=="revert-fail")?"\033[31m"$6"\033[0m":$6);
      printf "  %-5s %-7s %-9s %-26s %-15s %6s %6s\n",$1,$4,$5,c,$7,$8,$9}'
    printf "  total spent: ${CB}\$%s${C0}\n" "$(tail -1 "$ITERCSV" | awk -F, '{print $9}')"
  else
    echo "  (no iterations recorded yet)"
  fi

  # --- ClickHouse: recent runs with the syscall lever ---
  if clickhouse-client --query "SELECT 1" >/dev/null 2>&1; then
    printf "${CB}  recent runs (ClickHouse)  arm / score / conn_s / io_uring_enter·conn / drop / ceiling${C0}\n"
    clickhouse-client --query "SELECT arm, round(score,0) AS score, max_sustained_conn_s AS conn_s,
      round(sysc_io_uring_enter,2) AS enter_pc, round(drop_rate,5) AS drop, ceiling_reason AS ceiling
      FROM ${CH_DB}.runs ORDER BY ts DESC LIMIT 8 FORMAT PrettyCompactMonoBlock" 2>/dev/null | sed 's/^/  /'
  else
    printf "  ${CY}ClickHouse unreachable${C0} (loop still works; HISTORY.jsonl is the fallback)\n"
  fi

  # --- last few raw log lines ---
  if [ -f "$LOGOUT" ]; then
    printf "${CB}  ── tail %s ──${C0}\n" "$LOGOUT"
    tail -4 "$LOGOUT" 2>/dev/null | sed 's/^/  /'
  fi
}

if [ "${1:-}" = "--follow" ] || [ "${1:-}" = "-f" ]; then
  trap 'exit 0' INT
  while true; do clear; snapshot; printf "\n  (refresh ${INT}s · Ctrl-C to quit · stop loop: touch %s/STOP)\n" "$RESULTS_DIR"; sleep "$INT"; done
else
  snapshot
fi
