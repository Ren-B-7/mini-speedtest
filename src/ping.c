/*
 * ping.c  -  Latency measurement via TCP-connect probes
 *
 * Real ICMP ping requires elevated privileges.  We measure TCP-connect
 * latency to port 443/80 instead - accurate proxy for RTT, no root needed.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "speedtest.h"

#define MS_PER_SEC 1000.0
#define US_PER_MS 1000.0
#define PING_GAP_NS 200000000L

/* ------------------------------------------------------------------ */
/* helpers                                                              */
/* ------------------------------------------------------------------ */

static double now_ms(void)
{
    struct timeval time_val;
    gettimeofday(&time_val, NULL);
    return ((double)time_val.tv_sec * MS_PER_SEC) +
     ((double)time_val.tv_usec / US_PER_MS);
}

static double tcp_connect_ms(const char* host, const char* port)
{
    struct addrinfo hints;
    struct addrinfo* res;
    struct addrinfo* addr_iter;
    int socket_fd = -1;
    double elapsed = -1.0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        return -1.0;
    }

    double t_start = now_ms();

    for (addr_iter = res; addr_iter; addr_iter = addr_iter->ai_next) {
        socket_fd = socket(addr_iter->ai_family, addr_iter->ai_socktype,
         addr_iter->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }

        if (connect(socket_fd, addr_iter->ai_addr, addr_iter->ai_addrlen) ==
         0) {
            elapsed = now_ms() - t_start;
            close(socket_fd);
            break;
        }
        close(socket_fd);
    }

    freeaddrinfo(res);
    return elapsed;
}

/* ------------------------------------------------------------------ */
/* public API                                                           */
/* ------------------------------------------------------------------ */

PingResult ping_host(const char* host, int count)
{
    PingResult result;
    memset(&result, 0, sizeof result);

    if (!host || host[0] == '\0') {
        result.reachable = 0;
        result.latency_ms = -1.0;
        return result;
    }

    strncpy(result.host, host, sizeof result.host - 1);
    result.host[sizeof result.host - 1] = '\0';

    if (count < 1) {
        count = 4;
    }

    double total = 0.0;
    int success_count = 0;

    for (int i = 0; i < count; i++) {
        double latency_ms = tcp_connect_ms(host, "443");
        if (latency_ms < 0.0) {
            latency_ms = tcp_connect_ms(host, "80");
        }
        if (latency_ms >= 0.0) {
            total += latency_ms;
            success_count++;
        }
        struct timespec gap_time = {0, PING_GAP_NS};
        nanosleep(&gap_time, NULL);
    }

    if (success_count > 0) {
        result.reachable = 1;
        result.latency_ms = total / (double)success_count;
    } else {
        result.reachable = 0;
        result.latency_ms = -1.0;
    }

    return result;
}
