# research/ — how flashaccept was built (autonomous optimization of io_uring)

This directory is the complete, reproducible research rig that produced
[`flashaccept`](../). The optimized accept engine in the repo root wasn't hand-written — it was
**discovered by an autonomous optimizer** that mutated a baseline io_uring server in a closed
loop, measured each change against a fixed scoring function, and kept only what won.

## The question

A production Go proxy ("riptide") is **CPU-bound on `syscall6` in its accept path** at ~40k
conn/s — the goroutine-per-connection + one-syscall-per-op model spends most of its CPU just
getting connections in and out of the kernel. Could an io_uring accept path serve each connection
for materially **less CPU**? And rather than hand-tune it, could an AI agent find the optimum?

## The setup (two arms, one box, measured identically)

| arm | what it is |
|---|---|
| **control** (`control/`) | a faithful Go clone of riptide's accept path — `SO_REUSEPORT` fan-out, goroutine-per-connection. The CPU cost to beat. **Frozen.** |
| **treatment** (`treatment/`) | a C/liburing server, same observable behavior (accept → read request → write a fixed 19-byte reply → close). **The thing the optimizer mutates.** |

Both serve the identical contract and are benchmarked the same way by the **referee** (`harness/`,
`loadgen/`). The load generator (`loadgen/`) is a closed-loop, multi-source io_uring/epoll
connection storm; the harness pins the arm to a fixed core budget (cgroup v2 cpuset), ramps load,
runs an anti-cheat correctness gate (exact reply bytes + completion audit + <0.01% drops), and
scores it.

## The metric: CPU instructions per accepted connection

The objective is **`score = 1e9 / instructions_per_connection`** — minimize the CPU instructions
the accept path spends per connection (measured with `perf`, frequency-independent, averaged over
N reps at a fixed concurrency). This is exactly riptide's question: *accept each connection for
less CPU*. Throughput (conn/s per core) and a full profile (syscall counts, CPU kernel/user split,
hottest functions) are recorded alongside in ClickHouse.

## The optimizer: `claude -p` in a loop

The agent is **Claude Code in headless mode** (`harness/loop.sh` + `harness/optimizer-*.md`),
invoked once per iteration. Each iteration is two strictly separated phases:

- **ANALYTICS** — query ClickHouse + the profiles to see where the instructions go and what's
  been tried, spawn parallel sub-agents to explore the io_uring design space, then make **one**
  coherent change to `treatment/`. (CPU not under measurement here.)
- **MEASURE** — `harness/run.sh` builds, pins, loads, scores, and gates. ClickHouse is idle.

The bash harness — not the model — owns the verdict: **promote to champion only if the new score
beats the champion by >EPSILON; otherwise `git reset --hard`.** Every mutation is a git commit
with its hypothesis, so `git log` reconstructs the entire search. Memory lives in `kbs/` (distilled
lessons), git history, and ClickHouse (`acceptbench.*`).

## What it found

Starting from a textbook re-armed single-shot accept server, the optimizer discovered and kept:

1. **multishot accept** (`io_uring_prep_multishot_accept`) — one SQE yields many accept CQEs
2. **registered files / direct descriptors** — accept straight into the ring's table, skip fd-table churn
3. **per-worker connection freelist** — no per-connection `malloc`/`free`
4. **`MSG_MORE` reply+FIN fusion** — cork the 19-byte reply so `close()`'s FIN piggybacks onto it, collapsing two TCP segments (and two NIC doorbells) into one
5. **batched submit/harvest** — one `io_uring_enter` drives many connections

## The result (loopback, 1 core, CPU-bound, ~2–3% spread)

| server | instr / conn | conn/s (1 core) | vs Go | vs vanilla io_uring |
|---|---|---|---|---|
| Go goroutine-per-conn (riptide model) | 83,250 | 60,131 | 1.0× | — |
| vanilla io_uring (milestone-0) | 59,931 | 147,946 | 1.39× | 1.0× |
| **flashaccept** | **27,363** | **361,282** | **3.04×** | **2.19×** |

**flashaccept accepts each connection for ~3× fewer CPU instructions than the Go path and ~2.2×
fewer than vanilla io_uring** — about 6× and 2.4× the connections/sec on a single saturated core.

## Honest caveats

- This was developed on a **single VM** (loadgen + SUT co-resident) with the load generator
  reaching the SUT over a real network link. Connection-churn measurements **over that link are
  noisy** (±10–20%, network-condition-dependent retransmit work). The headline numbers above are
  the **loopback, CPU-bound** measurement, which isolates CPU-per-connection from network jitter —
  the correct environment for a "CPU per connection / connections per core" claim. The two-box
  Track-B deployment (`docs/ENVIRONMENT.md`) is needed for measurement-grade *throughput* numbers.
- The kernel TCP stack is ~94% of the remaining cost and is irreducible; flashaccept is near the
  practical floor for "accept + tiny reply + close" on this kernel.

## Reproduce it

```bash
sudo bash scripts/setup.sh        # toolchain, cgroups, ClickHouse, builds all arms (Ubuntu 24.04)
harness/run.sh control-frozen     # measure the Go baseline
harness/run.sh treatment          # measure the io_uring arm
harness/start-loop.sh             # run the autonomous optimizer (needs Claude Code; see docs/OPTIMIZER_HEADLESS.md)
harness/watch.sh --follow         # live terminal dashboard  (web dashboard: webui/server.py on :1000)
```

See `docs/` for the full contract, harness, metrics schema, and optimizer design.
