# Remote loadgen — the 2-box (Track B) setup

By default accept-bench runs everything on one box: the arm under test (SUT) and the load
generator share a host, and the loadgen hits the arm over loopback. That's fine for *building*
the rig, but it makes the **scores noisy and not measurement-grade** — the loadgen competes with
the SUT for CPU, memory bandwidth, and the kernel network stack, and a single VM has no clock-
frequency stability. Measured on the single box, run-to-run spread was **±14%**, which swamps any
real optimization the agent finds.

**Track B fixes this by moving the loadgen to a second machine.** Box 1 runs only the arm (pinned
to its core budget); box 2 runs only the load generator and drives traffic over a real NIC. Box 1's
harness talks to box 2 through a tiny **HTTP API** (`loadgen/loadgen-server.py`): for each load
pass it `GET`s `/run?...` and gets back the same JSON the local loadgen would print. Nothing else
about the benchmark changes — `run.sh`, the scoring, the gate, the optimizer loop are identical.

```
        BOX 1  (SUT)                              BOX 2  (load generator)
  ┌────────────────────────┐   /run?rate=..   ┌───────────────────────────┐
  │ harness/run.sh         │ ───────────────► │ loadgen-server.py  :8088  │
  │  arm on :30/:31 (1 core)│ ◄─────────────── │  -> runs ./loadgen        │
  │  ClickHouse, dashboard │   loadgen JSON   │  -> connects to box1:31    │
  └────────────────────────┘                  └───────────────────────────┘
            ▲      └───────── TCP connections (box2 → box1:31) ──────────┘
```

## Box 2 — set up the load generator

1. Get the code (clone the repo, or just copy the `loadgen/` dir and `scripts/setup-loadgen.sh`):
   ```bash
   git clone git@github.com:thealonlevi/hillclimb-io.git && cd hillclimb-io
   ```
2. Run the one-shot setup (builds loadgen, tunes the box as a high-rate connector, starts the API):
   ```bash
   sudo bash scripts/setup-loadgen.sh
   ```
   It prints box 2's API URL, e.g. `http://10.0.0.2:8088`. Verify:
   ```bash
   curl -s http://localhost:8088/health      # {"ok":true,"loadgen_present":true,...}
   ```
3. **Firewall:** box 2 must accept connections to the API port from box 1, and must be able to
   reach box 1 on the arm ports:
   ```bash
   # on box 2: allow box 1 -> box2:8088
   ufw allow from <box1-ip> to any port 8088 proto tcp   # (or your cloud security group)
   ```

The API port defaults to **8088** (override with `LOADGEN_API_PORT`). On systemd boxes setup
installs a `acceptbench-loadgen` service (auto-restart, survives reboot); otherwise it `nohup`s it.

## Box 1 — point the harness at box 2

Edit `harness/config` (or export before running):

```bash
TARGET_HOST="10.0.0.1"               # box 1's address, as reachable FROM box 2 (the arm binds 0.0.0.0)
LOADGEN_URL="http://10.0.0.2:8088"   # box 2's loadgen API
```

- `TARGET_HOST` is where box 2's loadgen connects — box 1's LAN/private IP, **not** `127.0.0.1`.
- **Firewall:** box 1 must accept `box2 -> box1:30` and `box2 -> box1:31`.

That's all. When `LOADGEN_URL` is non-empty, `run.sh` drives the remote loadgen; when empty it
falls back to the local binary (single-box dev). Verify from box 1:

```bash
curl -s http://10.0.0.2:8088/health
harness/run.sh control-frozen        # should ramp and score using box 2's traffic
```

Then run the optimizer loop exactly as before (`harness/start-loop.sh`) — it now measures over the
two-box path.

## Sizing box 2 (so it's never the bottleneck)

The whole point is that box 2 out-supplies the SUT, so the *SUT* is what saturates. Guidelines:

- **More cores than the SUT's budget.** The SUT is pinned to `CORES` (default 1). Box 2 should
  have several cores free for the loadgen; `LG_THREADS` (harness/config, default 10) sets its
  worker count. Verify box 2's CPU stays **< 60%** at the SUT's ceiling — if box 2 saturates,
  you're measuring box 2, not the SUT.
- **A real link.** 10GbE is ideal; at 19-byte replies the limit is packets/sec and the SYN/accept
  path, not bandwidth. Avoid a shared 1GbE if you're chasing >50k conn/s.
- **Connector tuning** (setup-loadgen.sh already applies it): wide `ip_local_port_range`,
  `tcp_tw_reuse=1`, low `tcp_fin_timeout`, conntrack off — otherwise box 2 exhausts ephemeral
  ports / TIME_WAIT well before the SUT's real ceiling.

## API reference

- `GET /health` → `{"ok":true,"loadgen_present":<bool>,"max_duration":<s>}`
- `GET /run?host=&port=&rate=&duration=&threads=&sample_pct=` → the loadgen JSON
  (`offered/completed/failed`, `drop_rate`, `reply_ok`, `p50_ms/p99_ms`, `fail_reasons`).
  `duration` is capped at `LOADGEN_MAX_DURATION` (default 120s). Output is byte-compatible with the
  local binary.

## Why this is the real fix

The single box can only measure to ±14%; the agent's real wins are a few percent, so they're
invisible and the greedy "keep best score" rule latches onto noise. Two boxes + a real NIC remove
the shared-host confounders and let you pin IRQs and lock CPU frequency, dropping the measurement
spread far enough that genuine optimizations clear the bar. This is the environment the benchmark
was designed for (see `docs/ENVIRONMENT.md`).
