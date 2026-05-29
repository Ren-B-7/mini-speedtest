/*
 * ping.c  -  Latency measurement via TCP-connect probes
 *
 * Real ICMP ping requires elevated privileges.  We measure TCP-connect
 * latency to port 443/80 instead - accurate proxy for RTT, no root needed.
 */
#define _POSIX_C_SOURCE 200112L  /* getaddrinfo, freeaddrinfo, nanosleep */
#define _DEFAULT_SOURCE          /* strdup */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "../include/speedtest.h"

/* ------------------------------------------------------------------ */
/* helpers                                                              */
/* ------------------------------------------------------------------ */

static double now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static double tcp_connect_ms(const char *host, const char *port)
{
    struct addrinfo hints, *res, *rp;
    int    fd      = -1;
    double elapsed = -1.0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0)
        return -1.0;

    double t_start = now_ms();

    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            elapsed = now_ms() - t_start;
            close(fd);
            fd = -1;
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return elapsed;
}

/* ------------------------------------------------------------------ */
/* public API                                                           */
/* ------------------------------------------------------------------ */

PingResult ping_host(const char *host, int count)
{
    PingResult result;
    memset(&result, 0, sizeof result);

    if (!host || host[0] == '\0') {
        result.reachable  = 0;
        result.latency_ms = -1.0;
        return result;
    }

    strncpy(result.host, host, sizeof result.host - 1);

    if (count < 1) count = 4;

    double total = 0.0;
    int    ok    = 0;

    for (int i = 0; i < count; i++) {
        double ms = tcp_connect_ms(host, "443");
        if (ms < 0.0)
            ms = tcp_connect_ms(host, "80");
        if (ms >= 0.0) {
            total += ms;
            ok++;
        }
        struct timespec ts = { 0, 200000000L }; /* 200 ms gap */
        nanosleep(&ts, NULL);
    }

    if (ok > 0) {
        result.reachable  = 1;
        result.latency_ms = total / ok;
    } else {
        result.reachable  = 0;
        result.latency_ms = -1.0;
    }

    return result;
}
