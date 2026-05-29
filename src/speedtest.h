#ifndef SPEEDTEST_H
#define SPEEDTEST_H

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Result containers                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    double latency_ms;
    int reachable;
    char host[256];
} PingResult;

typedef struct {
    double ttfb_ms;
    double total_ms;
    long http_code;
    int success;
} ConnResult;

typedef struct {
    char provider[64];
    char ip[64];
    char city[128];
    char region[128];
    char country[8];
    char org[256];
    double latitude;
    double longitude;
    double download_mbps;
    double upload_mbps;
    double ping_ms;
    int valid;
} SpeedResult;

/* ------------------------------------------------------------------ */
/* Provider IDs                                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    PROVIDER_IPAPI = 0,
    PROVIDER_IPINFO = 1,
    PROVIDER_CLOUDFLARE = 2,
    PROVIDER_FASTLY = 3,
    PROVIDER_HTTPBIN = 4,
    PROVIDER_ALL = 5,
    PROVIDER_UNKNOWN = -1
} Provider;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/* ping.c */
PingResult ping_host(const char* host, int count);

/* connection.c */
ConnResult measure_connection(const char* url);

/*
 * Measure sustained download throughput in Mbps.
 * extra_headers: NULL-terminated array of "Header: value" strings, or NULL.
 */
double measure_download(const char* url, const char* const* extra_headers);

/* json_parse.c */
SpeedResult parse_ipapi(const char* json);
SpeedResult parse_ipinfo(const char* json);
SpeedResult parse_cloudflare(const char* raw);
SpeedResult parse_fastly(const char* json);
SpeedResult parse_httpbin(const char* json);

/* providers.c */
Provider provider_from_string(const char* name);
const char* provider_to_string(Provider p);
SpeedResult run_provider(Provider p, const char* api_key);
void print_result(const SpeedResult* r, const PingResult* pr,
 const ConnResult* cr);
void list_providers(void);

#endif /* SPEEDTEST_H */
