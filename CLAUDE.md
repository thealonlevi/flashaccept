# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

**`flashaccept`** — a fast io_uring TCP accept engine for Linux, published as an importable C
library. It accepts connections for ~3× fewer CPU instructions than a goroutine-per-connection
server and ~2.2× fewer than vanilla io_uring (see `docs/BENCHMARKS.md`).

## Layout

- **Library (repo root):** `include/flashaccept.h` (public API), `src/flashaccept.c`
  (implementation), `Makefile` / `CMakeLists.txt` (builds `libflashaccept.a` + `.so`),
  `examples/echo_server.c`. Build: `make && make examples`.
- **`docs/`** — library docs: `API.md`, `BENCHMARKS.md`, `benchmark.svg`.
- **`research/`** — the complete, reproducible rig that *produced* the optimized engine: an
  autonomous `claude -p` optimizer that mutated a baseline io_uring server in a closed loop,
  scored each change on CPU-instructions-per-connection, and kept only what won. Contains the Go
  baseline (`control/`), the io_uring arm (`treatment/`), the referee (`harness/`, `loadgen/`),
  the dashboard (`webui/`), and `research/docs/` + `research/README.md` (the full story). Its
  scripts self-locate (`cd "$(dirname "$0")/.."` → `research/`), so run them from `research/`.

## Key facts

- The library is the **distilled artifact**; `research/` is **how it was found**. Keep that split.
- The optimized engine's wins: multishot accept, registered files/direct descriptors, per-worker
  connection freelist, `MSG_MORE` reply+FIN fusion, batched submit/harvest — all with graceful
  fallbacks for older kernels. Don't regress the hot path; justify accept-path changes with a
  measurement (instructions/conn — the `research/` rig measures it).
- Headline numbers are **loopback, CPU-bound** (isolates CPU/connection from network noise).
