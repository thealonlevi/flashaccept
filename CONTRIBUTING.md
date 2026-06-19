# Contributing to flashaccept

Thanks for your interest! flashaccept is a small, focused C library — a fast io_uring TCP accept
engine. Contributions that keep it lean and fast are very welcome.

## Building & testing

```bash
make                      # libflashaccept.a + libflashaccept.so
make examples             # examples/echo_server
./examples/echo_server --port 8080
```

## Ground rules

- **Keep the hot path lean.** This library exists to spend as few CPU instructions per connection
  as possible. Any change to the accept/recv/send/close path should be justified with a measurement
  (instructions/connection or conn/s on one pinned core). The `research/` directory has the full
  benchmark rig if you want to measure rigorously.
- **Graceful fallback.** Newer io_uring features (multishot accept needs kernel ≥5.19, direct
  descriptors, etc.) must degrade gracefully on older kernels, not crash.
- **No new dependencies** beyond liburing + pthreads.
- Match the surrounding style; keep public API changes additive and documented in `include/flashaccept.h`.

## Where things live

- Library: `include/`, `src/`, `Makefile`, `examples/`
- How it was built / how to benchmark: `research/` (the autonomous-optimizer rig)

Open an issue to discuss larger changes before a PR. Performance claims should come with a
reproducible measurement.
