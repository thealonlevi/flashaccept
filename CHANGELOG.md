# Changelog

All notable changes to flashaccept are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/); versions follow [SemVer](https://semver.org/).

## [1.0.0] — 2026-06-19

First public release. A fast, importable io_uring TCP accept engine for Linux.

### Added
- Public C API (`include/flashaccept.h`): `fa_server_new`, `fa_server_run`, `fa_server_stop`,
  `fa_server_free`, `fa_version`, plus `FLASHACCEPT_VERSION*` macros.
- Optimized accept engine (`src/flashaccept.c`): one io_uring + `SO_REUSEPORT` socket per worker,
  **multishot accept**, **registered files / direct descriptors**, **per-worker connection
  freelist**, **`MSG_MORE` reply+FIN fusion**, and **batched submit/harvest** — each with graceful
  fallback to older-kernel paths (single-shot accept, regular fds).
- Build system: `Makefile` (static + shared with SONAME `libflashaccept.so.1`, `install`/
  `uninstall`, generated `flashaccept.pc`) and `CMakeLists.txt`.
- `examples/echo_server.c`, docs (`docs/API.md`, `docs/BENCHMARKS.md`), CI, MIT license.

### Performance
Loopback, one pinned core, CPU-bound, fixed 512 in-flight, ~2–3% spread:

| server | instr/conn | conn/s (1 core) |
|---|---:|---:|
| Go goroutine-per-connection | 83,250 | 60,131 |
| vanilla io_uring | 59,931 | 147,946 |
| flashaccept | **27,363** | **361,282** |

→ 3.04× fewer CPU instructions per connection than the Go path, 2.19× vs vanilla io_uring.

### Verified
- AddressSanitizer + UBSan clean over 400k+ connections; clean `fa_server_stop()` shutdown.
- All four config paths exercised (multishot/single-shot × direct/regular-fd).

### Notes / limitations
- v1 supports the **request → reply → close** pattern (no keep-alive). The handler is invoked once
  per connection on the first batch of request bytes; it assumes the request fits in one `recv`.
- The optimized engine was **discovered by an autonomous optimizer** — see [`research/`](research/).

[1.0.0]: https://github.com/thealonlevi/flashaccept/releases/tag/v1.0.0
