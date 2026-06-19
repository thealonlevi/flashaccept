/*
 * flashaccept.h — a fast, importable io_uring TCP request/reply acceptor.
 *
 * MIT License
 *
 * Copyright (c) 2026 flashaccept contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * ---------------------------------------------------------------------------
 *
 * flashaccept is a tiny, embeddable engine for serving a very large number of
 * short-lived TCP connections with minimal CPU per connection. It is built on
 * Linux io_uring (liburing) and is designed for the classic accept-heavy
 * "one request in, one reply out, close" workload (HTTP-style health checks,
 * redirectors, tiny RPC replies, load-balancer probes, etc.).
 *
 * The engine handles everything that is hard to get fast:
 *   - one io_uring instance + one SO_REUSEPORT listening socket per worker
 *     thread, so accepts are kernel-load-balanced across cores with no shared
 *     state and no lock contention;
 *   - multishot accept (armed once; the kernel posts one CQE per connection),
 *     with graceful fallback to single-shot accept on older kernels;
 *   - registered files / direct descriptors (connections live in an io_uring
 *     descriptor table, skipping the process fd table), with graceful fallback
 *     to regular file descriptors;
 *   - a per-worker connection freelist (no per-connection malloc on the hot
 *     path);
 *   - MSG_MORE on the reply send so close()'s FIN piggybacks onto the reply,
 *     emitting the reply + FIN as a single TCP segment;
 *   - batched submit/harvest (one io_uring_enter flushes all queued work and
 *     reaps a whole completion batch).
 *
 * You supply only the policy: a callback that, given the request bytes, fills
 * in the reply to send. The engine owns the rest of the connection lifecycle.
 *
 * Threading model: fa_server_run() blocks the calling thread and spawns one
 * worker thread per configured CPU. Each worker is single-threaded and owns its
 * own ring, listening socket, and freelist, so your handler is invoked
 * concurrently across workers — keep it stateless or guard shared state
 * yourself. The handler must not block.
 *
 * Supported pattern (v1): request -> reply -> close. The handler is called once
 * per connection when the first batch of request bytes arrives; it returns the
 * reply length to send-then-close. Returning 0 closes the connection without a
 * reply. Keep-alive / multi-exchange connections are not supported in v1.
 */

#ifndef FLASHACCEPT_H
#define FLASHACCEPT_H

#define FLASHACCEPT_VERSION_MAJOR 1
#define FLASHACCEPT_VERSION_MINOR 0
#define FLASHACCEPT_VERSION_PATCH 0
#define FLASHACCEPT_VERSION       "1.0.0"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque server handle. Create with fa_server_new(), destroy with
 * fa_server_free(). */
typedef struct fa_server fa_server;

/*
 * Server configuration. Zero-initialize and override what you need; any field
 * left as 0 takes the documented default (so `fa_config cfg = {0}; cfg.port =
 * 8080;` is a valid minimal config).
 */
typedef struct {
    int port;          /* TCP port to listen on (required, no default).        */
    int workers;       /* Worker threads. 0 => one per available CPU
                          (sched_getaffinity). Each worker is pinned to a CPU.  */
    int backlog;       /* listen() backlog. 0 => 4096.                         */
    int recv_buf;      /* Per-connection request buffer size in bytes.
                          0 => 2048. This is also the maximum request size the
                          handler will see in one call.                         */
    int reply_cap;     /* Capacity handed to the handler for the reply, in
                          bytes. 0 => 2048. The handler must not write more.    */
    int multishot;     /* 1 => multishot accept (kernel >= 5.19) with fallback
                          to single-shot. 0 => force single-shot accept.        */
    int direct_files;  /* 1 => registered files / direct descriptors when the
                          kernel supports it, with fallback to regular fds.
                          0 => always use regular fds.                          */
} fa_config;

/*
 * Request handler. Invoked once per connection when request bytes arrive.
 *
 *   req     : pointer to the received request bytes (not NUL-terminated).
 *   req_len : number of valid bytes in `req` (> 0).
 *   reply   : output buffer to write the reply into.
 *   cap     : capacity of `reply` in bytes (cfg.reply_cap).
 *   user    : the opaque pointer passed to fa_server_new().
 *
 * Return value:
 *   > 0  : number of bytes written to `reply`; the engine sends exactly that
 *          many bytes and then closes the connection. Must be <= cap.
 *   == 0 : send nothing and close the connection immediately.
 *   < 0  : treated as 0 (close without reply).
 *
 * The handler runs on a worker thread inside the event loop and must not block.
 * It may be called concurrently from different worker threads.
 */
typedef int (*fa_handler)(const char *req, int req_len,
                          char *reply, int cap, void *user);

/*
 * Create a server. Copies `cfg`, so the caller's struct need not outlive the
 * call. `handler` is required; `user` is passed back to it verbatim.
 * Returns NULL on allocation failure or invalid configuration (e.g. port <= 0).
 * This call also raises RLIMIT_NOFILE toward the hard limit so the process can
 * hold many concurrent connections.
 */
fa_server *fa_server_new(const fa_config *cfg, fa_handler handler, void *user);

/*
 * Run the server. Creates the listening sockets and worker threads, then blocks
 * the calling thread until fa_server_stop() is invoked (or all workers exit).
 * Returns 0 on a clean stop, non-zero if the server failed to start.
 */
int fa_server_run(fa_server *s);

/*
 * Ask all workers to stop. Async-signal-safe, so it is safe to call from a
 * signal handler. After this returns, fa_server_run() will unblock shortly.
 */
void fa_server_stop(fa_server *s);

/*
 * Free a server handle and all its resources. The server must have stopped
 * (fa_server_run() must have returned) before calling this. Passing NULL is a
 * no-op.
 */
void fa_server_free(fa_server *s);

/* Library version string, e.g. "1.0.0". */
const char *fa_version(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASHACCEPT_H */
