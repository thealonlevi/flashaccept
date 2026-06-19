# flashaccept API

A small C API. Include `<flashaccept.h>` and link `-lflashaccept -luring -lpthread`.

## Model

flashaccept runs one `io_uring` instance and one `SO_REUSEPORT` listening socket **per worker**
(one per CPU by default); the kernel load-balances accepts across them. For each connection it does
**accept → recv request → call your handler → send the handler's reply (MSG_MORE-fused with the
FIN) → close**. This is the request→reply→close pattern (no keep-alive in v1).

## Types

```c
typedef struct fa_server fa_server;

typedef struct {
    int port;          // TCP port to listen on (required)
    int workers;       // 0 => one worker per available CPU (sched_getaffinity)
    int backlog;       // 0 => 4096
    int recv_buf;      // 0 => 2048  (max request bytes seen per handler call)
    int reply_cap;     // 0 => 2048  (capacity of the reply buffer given to the handler)
    int multishot;     // 1 => multishot accept (kernel >=5.19), with single-shot fallback
    int direct_files;  // 1 => registered files / direct descriptors, with regular-fd fallback
} fa_config;

// Called when request bytes arrive. Write up to `cap` bytes into `reply`.
// Return >0 = number of reply bytes to send, then close the connection.
// Return 0 or <0 = close without replying.
typedef int (*fa_handler)(const char *req, int req_len, char *reply, int cap, void *user);
```

## Functions

```c
fa_server *fa_server_new(const fa_config *cfg, fa_handler handler, void *user);
int   fa_server_run(fa_server *s);   // blocks; runs all workers until fa_server_stop()
void  fa_server_stop(fa_server *s);  // async-signal-safe; ask workers to stop (call from a signal handler)
void  fa_server_free(fa_server *s);
const char *fa_version(void);        // e.g. "1.0.0"
```

`fa_server_new` returns `NULL` on failure. `fa_server_run` blocks the calling thread and runs all
workers; it returns when stopped. Multiple `fa_server` instances are supported (no global state).

## Notes

- **Kernel features degrade gracefully:** if multishot accept or direct descriptors aren't
  available (older kernel, or registration fails), flashaccept falls back to re-armed single-shot
  accept and regular fds — it still runs, just a little less optimally.
- **The reply buffer** passed to your handler is per-worker scratch, valid until you return; copy
  your reply into it.
- flashaccept raises `RLIMIT_NOFILE` on `fa_server_run` so it can hold many concurrent connections.

## Example

See [`examples/echo_server.c`](../examples/echo_server.c).
