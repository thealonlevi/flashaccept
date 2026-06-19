/*
 * echo_server.c — minimal flashaccept example.
 *
 * MIT License. Copyright (c) 2026 flashaccept contributors.
 *
 * Listens on a TCP port and answers every connection with a fixed 19-byte
 * HTTP reply, then closes — the classic accept-heavy "tiny reply" workload.
 *
 * Build:  make examples
 * Run:    ./examples/echo_server --port 12480
 */

#define _GNU_SOURCE
#include "flashaccept.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* Exactly 19 bytes — "HTTP/1.1 200 OK\r\n\r\n". */
static const char REPLY[] = "HTTP/1.1 200 OK\r\n\r\n";

/* Reply handler: ignore the request, send the fixed reply, then close. */
static int handler(const char *req, int req_len,
                   char *reply, int cap, void *user)
{
    (void)req; (void)req_len; (void)user;
    int n = (int)sizeof(REPLY) - 1;     /* 19, excluding the NUL */
    if (n > cap)
        n = cap;
    memcpy(reply, REPLY, (size_t)n);
    return n;                           /* send n bytes, then close */
}

static fa_server *g_server;

static void on_signal(int sig)
{
    (void)sig;
    fa_server_stop(g_server);           /* async-signal-safe */
}

int main(int argc, char **argv)
{
    int port = 12480, multishot = 1, direct = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc)        port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--multishot") && i + 1 < argc) multishot = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--direct") && i + 1 < argc)    direct = atoi(argv[++i]);
    }

    fa_config cfg = {0};
    cfg.port         = port;
    cfg.workers      = 0;          /* one worker per available CPU */
    cfg.multishot    = multishot;  /* 1 = multishot accept (with fallback to single-shot) */
    cfg.direct_files = direct;     /* 1 = registered/direct descriptors (with fallback) */

    g_server = fa_server_new(&cfg, handler, NULL);
    if (!g_server) {
        fprintf(stderr, "echo_server: failed to create server\n");
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "echo_server (flashaccept %s): listening on port %d\n",
            fa_version(), port);

    int rc = fa_server_run(g_server);   /* blocks until a signal */
    fa_server_free(g_server);
    return rc;
}
