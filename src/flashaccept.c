/*
 * flashaccept.c — implementation of the flashaccept io_uring TCP acceptor.
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
 * Engine overview (the optimizations, all preserved from the original):
 *   - One worker thread per configured CPU; each thread pinned to its CPU and
 *     owning its own io_uring instance and its own SO_REUSEPORT listening
 *     socket. The kernel load-balances incoming connections across the
 *     reuseport sockets, so there is no shared accept state between cores.
 *   - Multishot accept: armed once per worker, the kernel keeps it standing and
 *     posts one accept CQE per connection without re-consuming an SQE. Falls
 *     back to re-armed single-shot accept if multishot is disabled/unsupported.
 *   - Registered files / direct descriptors: connections are accepted into an
 *     io_uring-owned descriptor table (auto-allocated slots), and recv/send/
 *     close reference the slot index with IOSQE_FIXED_FILE — skipping the fd
 *     install on accept and the per-op fd-table lookup. Falls back to regular
 *     fds if registration fails or is disabled.
 *   - Per-worker connection freelist: no per-connection malloc on the hot path.
 *   - MSG_MORE on the reply send corks the reply so the subsequent close()'s
 *     FIN piggybacks onto it — the reply and FIN leave as one TCP segment.
 *   - Batched submit/harvest: one io_uring_submit_and_wait flushes all queued
 *     SQEs and blocks for completions, then the whole CQ batch is drained
 *     before the next enter, amortizing io_uring_enter across many completions.
 *
 * Per-connection state machine (request -> reply -> close):
 *   ACCEPT -> RECV -> (handler) -> SEND(reply, MSG_MORE) + CLOSE -> freed.
 * The reply send and the close are queued back-to-back with IOSQE_CQE_SKIP_
 * SUCCESS and NULL user_data, so the success path posts no completion: only
 * accept + recv generate CQEs (~2 per connection).
 */

#define _GNU_SOURCE
#include "flashaccept.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <liburing.h>

/* flashaccept's fast path uses multishot accept, direct descriptors, and
 * SINGLE_ISSUER/DEFER_TASKRUN — all liburing >= 2.3 (Ubuntu 24.04+, or build
 * liburing from source). Fail with a clear message instead of a cryptic
 * "IORING_FILE_INDEX_ALLOC undeclared" on older liburing (e.g. Ubuntu 22.04's 2.1). */
#if !defined(IO_URING_VERSION_MAJOR) || (IO_URING_VERSION_MAJOR < 2) || \
    (IO_URING_VERSION_MAJOR == 2 && IO_URING_VERSION_MINOR < 3)
#error "flashaccept requires liburing >= 2.3. Upgrade liburing-dev, or build it from source: https://github.com/axboe/liburing"
#endif

#define FA_VERSION_STR "1.0.0"

/* Tunables that are not worth exposing in the public API. */
#define FA_QUEUE_DEPTH   4096   /* io_uring SQ/CQ depth per worker.            */
#define FA_NFILES        4096   /* direct-descriptor table size per worker.    */
#define FA_CQE_BATCH      256   /* CQEs reaped per io_uring_enter.             */
#define FA_FREELIST_WARM  128   /* conns pre-allocated onto the freelist.      */
#define FA_STOP_POLL_NS  (200 * 1000 * 1000) /* 200ms stop-flag poll cadence.  */

/* ------------------------------------------------------------------------- */
/* Per-connection state                                                      */
/* ------------------------------------------------------------------------- */

enum fa_conn_state {
    FA_ST_ACCEPT = 0,  /* the standing accept marker (CQE res = client fd)     */
    FA_ST_RECV,        /* reading the client's request bytes                   */
};

struct fa_conn {
    struct fa_conn *next;  /* freelist link (valid only while on the freelist) */
    enum fa_conn_state state;
    int  fd;               /* direct-descriptor index when direct, else a fd   */
    char buf[];            /* request scratch buffer, cfg.recv_buf bytes        */
};

/* ------------------------------------------------------------------------- */
/* Per-worker context                                                        */
/* ------------------------------------------------------------------------- */

struct fa_worker {
    struct io_uring ring;
    int   listen_fd;
    int   direct;            /* 1 if the registered file table is active        */
    int   cpu;               /* CPU to pin to, or -1                            */
    int   ring_ready;        /* 1 once the ring is initialized (for teardown)   */
    struct fa_conn accept_marker;  /* sentinel conn for the accept SQE          */
    struct fa_conn *free_head;     /* per-worker conn freelist (no lock needed) */
    fa_server *srv;          /* back-pointer for config + handler               */
    pthread_t thread;
    char reply[];            /* per-worker reply scratch, cfg.reply_cap bytes   */
};

/* ------------------------------------------------------------------------- */
/* Server handle                                                             */
/* ------------------------------------------------------------------------- */

struct fa_server {
    fa_config   cfg;
    fa_handler  handler;
    void       *user;

    int         conn_size;   /* sizeof(struct fa_conn) + recv_buf, cached       */
    int         nworkers;

    struct fa_worker **workers;   /* array of nworkers worker pointers           */

    volatile sig_atomic_t stop;   /* set by fa_server_stop()                     */
};

/* ------------------------------------------------------------------------- */
/* Connection freelist (per worker, single-threaded)                         */
/* ------------------------------------------------------------------------- */

static struct fa_conn *fa_conn_alloc(struct fa_worker *w)
{
    if (w->free_head) {
        struct fa_conn *c = w->free_head;
        w->free_head = c->next;
        return c;
    }
    return malloc((size_t)w->srv->conn_size);
}

static void fa_conn_free(struct fa_worker *w, struct fa_conn *c)
{
    c->next = w->free_head;
    w->free_head = c;
}

/* ------------------------------------------------------------------------- */
/* Listening socket                                                          */
/* ------------------------------------------------------------------------- */

static int fa_make_listen_socket(int port, int backlog)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, backlog) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* ------------------------------------------------------------------------- */
/* SQE helpers                                                               */
/* ------------------------------------------------------------------------- */

/*
 * Arm an accept. With multishot the kernel keeps the request standing and posts
 * one accept CQE per connection without consuming further SQEs; we only re-arm
 * if multishot terminates. With single-shot we re-arm one accept per connection.
 */
static void fa_arm_accept(struct fa_worker *w)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&w->ring);
    if (!sqe) {
        io_uring_submit(&w->ring);
        sqe = io_uring_get_sqe(&w->ring);
        if (!sqe)
            return;
    }

    int multishot = w->srv->cfg.multishot;
    if (w->direct) {
        if (multishot)
            io_uring_prep_multishot_accept_direct(sqe, w->listen_fd, NULL, NULL, 0);
        else
            io_uring_prep_accept_direct(sqe, w->listen_fd, NULL, NULL, 0,
                                        IORING_FILE_INDEX_ALLOC);
    } else {
        if (multishot)
            io_uring_prep_multishot_accept(sqe, w->listen_fd, NULL, NULL, 0);
        else
            io_uring_prep_accept(sqe, w->listen_fd, NULL, NULL, 0);
    }
    w->accept_marker.state = FA_ST_ACCEPT;
    io_uring_sqe_set_data(sqe, &w->accept_marker);
}

/* Queue a recv for the connection's request bytes. */
static void fa_submit_recv(struct fa_worker *w, struct fa_conn *c)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&w->ring);
    if (!sqe) {
        io_uring_submit(&w->ring);
        sqe = io_uring_get_sqe(&w->ring);
        if (!sqe) {
            /* Deep-queue exhaustion (effectively unreachable): drop cleanly. */
            if (!w->direct)
                close(c->fd);
            fa_conn_free(w, c);
            return;
        }
    }
    c->state = FA_ST_RECV;
    io_uring_prep_recv(sqe, c->fd, c->buf, w->srv->cfg.recv_buf, 0);
    if (w->direct)
        io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE); /* c->fd is a table index */
    io_uring_sqe_set_data(sqe, c);
}

/*
 * Queue a close with no completion (CQE_SKIP_SUCCESS, NULL data) and free the
 * conn. Used on the error path (recv <= 0) and for the close after a reply.
 */
static void fa_submit_close(struct fa_worker *w, struct fa_conn *c)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&w->ring);
    if (!sqe) {
        io_uring_submit(&w->ring);
        sqe = io_uring_get_sqe(&w->ring);
        if (!sqe) {
            if (!w->direct)
                close(c->fd);
            fa_conn_free(w, c);
            return;
        }
    }
    if (w->direct)
        io_uring_prep_close_direct(sqe, c->fd); /* frees the registered slot */
    else
        io_uring_prep_close(sqe, c->fd);
    io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
    io_uring_sqe_set_data(sqe, NULL);
    fa_conn_free(w, c);
}

/*
 * Send `reply_len` reply bytes then close, both with ZERO completions. The send
 * uses MSG_MORE to cork the reply, so the close's FIN piggybacks onto it and the
 * reply + FIN leave as a single TCP segment. Both SQEs carry IOSQE_CQE_SKIP_
 * SUCCESS + NULL user_data, so the success path posts no CQE. The conn is freed
 * immediately: fd is captured into both SQEs at prep time, and the reply bytes
 * live in the per-worker reply scratch (stable until the next connection on this
 * worker, which cannot start until after these SQEs are submitted). A rare
 * FAILURE posts a CQE with NULL data, ignored by the drain loop's !c guard.
 */
static void fa_submit_reply_and_close(struct fa_worker *w, struct fa_conn *c,
                                      const char *reply, int reply_len)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&w->ring);
    if (!sqe) {
        io_uring_submit(&w->ring);
        sqe = io_uring_get_sqe(&w->ring);
        if (!sqe) {
            if (!w->direct)
                close(c->fd);
            fa_conn_free(w, c);
            return;
        }
    }
    io_uring_prep_send(sqe, c->fd, reply, (unsigned)reply_len, MSG_MORE);
    io_uring_sqe_set_flags(sqe, w->direct
                           ? (IOSQE_FIXED_FILE | IOSQE_CQE_SKIP_SUCCESS)
                           : IOSQE_CQE_SKIP_SUCCESS);
    io_uring_sqe_set_data(sqe, NULL);

    sqe = io_uring_get_sqe(&w->ring);
    if (!sqe) {
        /* Reply is queued; flush it and close synchronously so we never hang. */
        io_uring_submit(&w->ring);
        sqe = io_uring_get_sqe(&w->ring);
        if (!sqe) {
            if (!w->direct)
                close(c->fd);
            fa_conn_free(w, c);
            return;
        }
    }
    if (w->direct)
        io_uring_prep_close_direct(sqe, c->fd);
    else
        io_uring_prep_close(sqe, c->fd);
    io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
    io_uring_sqe_set_data(sqe, NULL);
    fa_conn_free(w, c);
}

/* ------------------------------------------------------------------------- */
/* Worker event loop                                                         */
/* ------------------------------------------------------------------------- */

static void fa_worker_loop(struct fa_worker *w)
{
    fa_server *s = w->srv;
    struct io_uring_cqe *cqes[FA_CQE_BATCH];

    /* Pre-warm the freelist so hot-path accepts never hit malloc. */
    for (int i = 0; i < FA_FREELIST_WARM; i++) {
        struct fa_conn *c = malloc((size_t)s->conn_size);
        if (c)
            fa_conn_free(w, c);
    }

    fa_arm_accept(w);

    /* Wake periodically (timeout) so we can observe the stop flag even with no
     * traffic. The timeout is the only overhead added over the original tight
     * loop, and it is negligible at high connection churn. */
    struct __kernel_timespec ts = {
        .tv_sec  = FA_STOP_POLL_NS / 1000000000L,
        .tv_nsec = FA_STOP_POLL_NS % 1000000000L,
    };

    while (!s->stop) {
        int ret = io_uring_submit_and_wait_timeout(&w->ring, cqes, 1, &ts, NULL);
        if (ret < 0) {
            if (ret == -ETIME || ret == -EINTR)
                continue;        /* timed out / interrupted: re-check stop flag */
            fprintf(stderr, "flashaccept: submit_and_wait: %s\n", strerror(-ret));
            break;
        }

        /* Drain the whole completion batch before the next enter. */
        unsigned n = io_uring_peek_batch_cqe(&w->ring, cqes, FA_CQE_BATCH);
        for (unsigned i = 0; i < n; i++) {
            struct io_uring_cqe *cqe = cqes[i];
            struct fa_conn *c = io_uring_cqe_get_data(cqe);
            /* NULL data => a send/close completion (only posted on rare
             * failure, since success is CQE_SKIP_SUCCESS); skip it. */
            if (!c)
                continue;
            int res = cqe->res;

            if (c->state == FA_ST_ACCEPT) {
                /* Re-arm if the accept request terminated. Multishot stays
                 * armed (F_MORE set) until a fatal error; single-shot always
                 * needs re-arming. */
                if (!(cqe->flags & IORING_CQE_F_MORE))
                    fa_arm_accept(w);
                if (res < 0)
                    continue;    /* transient accept error; keep going */

                struct fa_conn *nc = fa_conn_alloc(w);
                if (!nc) {
                    if (!w->direct)
                        close(res);   /* direct slot leaks only on OOM */
                    continue;
                }
                nc->fd = res;        /* client fd, or direct table index */
                fa_submit_recv(w, nc);
            } else { /* FA_ST_RECV */
                if (res <= 0) {
                    fa_submit_close(w, c);   /* EOF or error: close */
                } else {
                    /* Request bytes drained; ask the user what to reply. */
                    int rlen = s->handler(c->buf, res, w->reply,
                                          s->cfg.reply_cap, s->user);
                    if (rlen > 0) {
                        if (rlen > s->cfg.reply_cap)
                            rlen = s->cfg.reply_cap;
                        fa_submit_reply_and_close(w, c, w->reply, rlen);
                    } else {
                        fa_submit_close(w, c);  /* handler declined: close */
                    }
                }
            }
        }
        io_uring_cq_advance(&w->ring, n);
    }
}

/* ------------------------------------------------------------------------- */
/* Worker setup / thread entry                                               */
/* ------------------------------------------------------------------------- */

static void *fa_thread_entry(void *arg)
{
    struct fa_worker *w = arg;

    if (w->cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(w->cpu, &set);
        pthread_setaffinity_np(pthread_self(), sizeof set, &set);
    }

    /* Initialize the ring in the SAME thread that submits/reaps it so the
     * SINGLE_ISSUER contract holds. SINGLE_ISSUER + DEFER_TASKRUN move
     * completion task-work into our GETEVENTS enter and drop cross-CPU wakeup
     * overhead. Fall back to a plain ring if the kernel rejects the flags. */
    struct io_uring_params p;
    memset(&p, 0, sizeof p);
    p.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
    int ret = io_uring_queue_init_params(FA_QUEUE_DEPTH, &w->ring, &p);
    if (ret < 0) {
        ret = io_uring_queue_init(FA_QUEUE_DEPTH, &w->ring, 0);
        if (ret < 0) {
            fprintf(stderr, "flashaccept: io_uring_queue_init: %s\n",
                    strerror(-ret));
            return NULL;
        }
    }
    w->ring_ready = 1;

    /* Register a sparse direct-descriptor table if requested. Connections are
     * accepted into auto-allocated slots referenced with IOSQE_FIXED_FILE,
     * skipping the fd install on accept and the per-op fd lookup. Falls back to
     * regular fds if registration fails. */
    if (w->srv->cfg.direct_files &&
        io_uring_register_files_sparse(&w->ring, FA_NFILES) == 0)
        w->direct = 1;

    fa_worker_loop(w);
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Misc                                                                      */
/* ------------------------------------------------------------------------- */

/* Raise the open-file soft limit toward the hard limit. Best effort. */
static void fa_raise_nofile(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < rl.rlim_max) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
}

/* Collect the CPU ids in the process's affinity mask. Returns count, or a
 * single -1 entry (count 1) if affinity can't be read. */
static int fa_collect_cpus(int *out, int max)
{
    cpu_set_t aff;
    CPU_ZERO(&aff);
    if (sched_getaffinity(0, sizeof aff, &aff) < 0) {
        out[0] = -1;
        return 1;
    }
    int n = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE && n < max; cpu++) {
        if (CPU_ISSET(cpu, &aff))
            out[n++] = cpu;
    }
    if (n == 0) {
        out[0] = -1;
        return 1;
    }
    return n;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

fa_server *fa_server_new(const fa_config *cfg, fa_handler handler, void *user)
{
    if (!cfg || !handler || cfg->port <= 0)
        return NULL;

    fa_server *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;

    s->cfg     = *cfg;
    s->handler = handler;
    s->user    = user;
    s->stop    = 0;

    /* Apply defaults for zero-valued fields. */
    if (s->cfg.backlog   <= 0) s->cfg.backlog   = 4096;
    if (s->cfg.recv_buf  <= 0) s->cfg.recv_buf  = 2048;
    if (s->cfg.reply_cap <= 0) s->cfg.reply_cap = 2048;
    /* multishot/direct_files default to 0; callers usually want them on, so the
     * documented "fast path" examples set them explicitly. */

    s->conn_size = (int)(sizeof(struct fa_conn) + (size_t)s->cfg.recv_buf);

    /* Decide worker count + the CPUs to pin them to. */
    int cpus[CPU_SETSIZE];
    int ncpu = fa_collect_cpus(cpus, CPU_SETSIZE);
    int nworkers = s->cfg.workers > 0 ? s->cfg.workers : ncpu;
    s->nworkers = nworkers;

    s->workers = calloc((size_t)nworkers, sizeof *s->workers);
    if (!s->workers) {
        free(s);
        return NULL;
    }

    /* Allocate each worker (variable-sized: includes the reply scratch). */
    size_t wsz = sizeof(struct fa_worker) + (size_t)s->cfg.reply_cap;
    for (int i = 0; i < nworkers; i++) {
        struct fa_worker *w = calloc(1, wsz);
        if (!w) {
            for (int j = 0; j < i; j++)
                free(s->workers[j]);
            free(s->workers);
            free(s);
            return NULL;
        }
        w->srv = s;
        w->listen_fd = -1;
        /* Pin to a CPU only when running one-worker-per-CPU; if the caller
         * over/under-subscribes explicitly, round-robin across allowed CPUs. */
        w->cpu = cpus[i % ncpu];
        s->workers[i] = w;
    }

    return s;
}

int fa_server_run(fa_server *s)
{
    if (!s)
        return -1;

    fa_raise_nofile();

    /* Create one listening socket per worker (each with SO_REUSEPORT). */
    for (int i = 0; i < s->nworkers; i++) {
        int fd = fa_make_listen_socket(s->cfg.port, s->cfg.backlog);
        if (fd < 0) {
            fprintf(stderr, "flashaccept: failed to create listen socket "
                    "for worker %d on port %d\n", i, s->cfg.port);
            /* Clean up any sockets opened so far. */
            for (int j = 0; j < i; j++) {
                close(s->workers[j]->listen_fd);
                s->workers[j]->listen_fd = -1;
            }
            return -1;
        }
        s->workers[i]->listen_fd = fd;
    }

    /* Launch worker threads. */
    int started = 0;
    for (int i = 0; i < s->nworkers; i++) {
        if (pthread_create(&s->workers[i]->thread, NULL,
                           fa_thread_entry, s->workers[i]) != 0) {
            fprintf(stderr, "flashaccept: pthread_create failed for worker %d\n", i);
            s->stop = 1;
            break;
        }
        started++;
    }

    if (started == 0)
        return -1;

    for (int i = 0; i < started; i++)
        pthread_join(s->workers[i]->thread, NULL);

    /* Tear down rings and listening sockets so the server can be re-run/freed. */
    for (int i = 0; i < s->nworkers; i++) {
        struct fa_worker *w = s->workers[i];
        if (w->ring_ready) {
            io_uring_queue_exit(&w->ring);
            w->ring_ready = 0;
        }
        if (w->listen_fd >= 0) {
            close(w->listen_fd);
            w->listen_fd = -1;
        }
        /* Drain this worker's freelist. */
        struct fa_conn *c = w->free_head;
        while (c) {
            struct fa_conn *next = c->next;
            free(c);
            c = next;
        }
        w->free_head = NULL;
    }

    return 0;
}

void fa_server_stop(fa_server *s)
{
    if (s)
        s->stop = 1;   /* sig_atomic_t store is async-signal-safe */
}

void fa_server_free(fa_server *s)
{
    if (!s)
        return;
    if (s->workers) {
        for (int i = 0; i < s->nworkers; i++)
            free(s->workers[i]);
        free(s->workers);
    }
    free(s);
}

const char *fa_version(void)
{
    return FA_VERSION_STR;
}
