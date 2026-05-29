/*
 * providers.c  -  Provider registry and orchestration
 *
 * Each provider descriptor holds the URL, ping host, Accept header,
 * and parser function pointer.  run_provider() wires everything together:
 * fetch -> parse -> ping -> connection metrics -> print.
 */

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "curl_helper.h"
#include "speedtest.h"

#define EPSILON 0.0001

/* ------------------------------------------------------------------ */
/* ANSI colours                                                         */
/* ------------------------------------------------------------------ */
#define COL_RESET "\033[0m"
#define COL_BOLD "\033[1m"
#define COL_CYAN "\033[36m"
#define COL_GREEN "\033[32m"
#define COL_YELLOW "\033[33m"
#define COL_RED "\033[31m"
#define COL_MAGENTA "\033[35m"
#define COL_BLUE "\033[34m"
#define COL_GREY "\033[90m"

/* ------------------------------------------------------------------ */
/* Provider descriptor                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    Provider id;
    const char* name;
    const char* slug;
    const char* url;
    const char* download_url_str;
    const char* const* download_headers; /* NULL-terminated, or NULL */
    const char* ping_host_str;
    const char* accept;
    SpeedResult (*parse)(const char* body);
} ProviderDef;

/* ------------------------------------------------------------------ */
/* Provider table                                                       */
/* ------------------------------------------------------------------ */

/* Cloudflare's __down endpoint requires a Referer to serve payload bytes */
static const char* const CF_DOWN_HEADERS[] = {
    "Referer: https://speed.cloudflare.com/", NULL};

/*
 * Download URL notes:
 *   Cloudflare – speed.cloudflare.com/__down is Cloudflare's own public
 *     speed-test API; ?bytes= controls payload size (120 MB used here so the
 *     10-second cap in measure_download() gives a meaningful Mbps reading on
 *     fast links without wasting bandwidth on slow ones).
 *
 *   ip-api / ipinfo / httpbin – no public large-file CDN endpoint; fall back
 *     to Hetzner's publicly-hosted speed-test files (explicitly provided for
 *     bandwidth testing, no authentication required, multiple DCs available).
 *     nbg1  = Nuremberg (EU),  ash = Ashburn (US East).
 *
 *   Fastly – speedtest.nyc1.fastly.net does not resolve publicly; replaced
 *     with the same Hetzner US endpoint so the provider still exercises a
 *     real CDN path for the ip/geo lookup while using a reliable download.
 */
static const ProviderDef PROVIDERS[] = {
    {PROVIDER_IPAPI, "ip-api.com", "ipapi",
        "http://ip-api.com/json/?fields=status,message,country,countryCode,"
        "regionName,city,lat,lon,isp,org,query",
        "https://nbg1-speed.hetzner.com/100MB.bin", NULL, "ip-api.com",
        "Accept: application/json", parse_ipapi},
    {PROVIDER_IPINFO, "ipinfo.io", "ipinfo", "https://ipinfo.io/json",
        "https://nbg1-speed.hetzner.com/100MB.bin", NULL, "ipinfo.io",
        "Accept: application/json", parse_ipinfo},
    {PROVIDER_CLOUDFLARE, "Cloudflare trace", "cloudflare",
        "https://one.one.one.one/cdn-cgi/trace",
        "https://speed.cloudflare.com/__down?bytes=125829120", CF_DOWN_HEADERS,
        "one.one.one.one", NULL, parse_cloudflare},
    {PROVIDER_FASTLY, "Fastly edge", "fastly",
        "https://api.fastly.com/public-ip-list",
        "https://ash-speed.hetzner.com/100MB.bin", NULL, "api.fastly.com",
        "Accept: application/json", parse_fastly},
    {PROVIDER_HTTPBIN, "httpbin.org", "httpbin", "https://httpbin.org/get",
        "https://nbg1-speed.hetzner.com/100MB.bin", NULL, "httpbin.org",
        "Accept: application/json", parse_httpbin},
};

static const int PROVIDER_COUNT = (int)(sizeof PROVIDERS / sizeof PROVIDERS[0]);

/* ------------------------------------------------------------------ */
/* Name <-> ID helpers                                                  */
/* ------------------------------------------------------------------ */

Provider provider_from_string(const char* name)
{
    if (!name) {
        return PROVIDER_UNKNOWN;
    }
    if (strcmp(name, "all") == 0) {
        return PROVIDER_ALL;
    }
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        if (strcmp(name, PROVIDERS[i].slug) == 0 ||
         strcmp(name, PROVIDERS[i].name) == 0) {
            return PROVIDERS[i].id;
        }
    }
    return PROVIDER_UNKNOWN;
}

const char* provider_to_string(Provider provider)
{
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        if (PROVIDERS[i].id == provider) {
            return PROVIDERS[i].name;
        }
    }
    return "unknown";
}

void list_providers(void)
{
    printf(COL_BOLD "\nAvailable providers:\n" COL_RESET);
    printf("  %-20s  %s\n", "Slug (--provider=)", "Description");
    printf("  %-20s  %s\n", "--------------------", "-----------");
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        printf("  " COL_CYAN "%-20s" COL_RESET "  %s\n", PROVIDERS[i].slug,
         PROVIDERS[i].name);
    }
    printf("  " COL_CYAN "%-20s" COL_RESET "  Run all providers\n\n", "all");
}

/* ------------------------------------------------------------------ */
/* ASCII progress bar                                                   */
/* ------------------------------------------------------------------ */

static void bar(double value, double max, int width, const char* colour)
{
    int filled = (int)((value / max) * width);
    if (filled < 0) {
        filled = 0;
    }
    if (filled > width) {
        filled = width;
    }
    printf("%s", colour);
    for (int i = 0; i < filled; i++) {
        printf("#");
    }
    printf(COL_GREY);
    for (int i = filled; i < width; i++) {
        printf(".");
    }
    printf(COL_RESET);
}

#define PING_SCALE_MAX 200.0
#define TTFB_GOOD_MS 200.0
#define TTFB_WARN_MS 600.0
#define TTFB_SCALE_MAX 1000.0
#define VISUAL_BAR_WIDTH 20

/* ------------------------------------------------------------------ */
/* print_result                                                         */
/* ------------------------------------------------------------------ */
void print_result(const SpeedResult* result, const PingResult* ping_result,
 const ConnResult* conn_result)
{
    if (!result->valid) {
        printf(COL_RED "  x Provider returned no usable data.\n" COL_RESET);
        return;
    }

    printf(COL_BOLD "  %-12s" COL_RESET " %s\n", "Provider:", result->provider);
    printf(COL_BOLD "  %-12s" COL_RESET " %s\n", "Public IP:", result->ip);

    if (result->city[0]) {
        printf(COL_BOLD "  %-12s" COL_RESET " %s\n", "City:", result->city);
    }
    if (result->region[0]) {
        printf(COL_BOLD "  %-12s" COL_RESET " %s\n", "Region:", result->region);
    }
    if (result->country[0]) {
        printf(COL_BOLD "  %-12s" COL_RESET " %s\n",
         "Country:", result->country);
    }
    if (result->org[0]) {
        printf(COL_BOLD "  %-12s" COL_RESET " %s\n", "Org/ISP:", result->org);
    }

    if (fabs(result->latitude) > EPSILON || fabs(result->longitude) > EPSILON) {
        printf(COL_BOLD "  %-12s" COL_RESET " %.4f, %.4f\n",
         "Coords:", result->latitude, result->longitude);
    }

    /* Ping */
    printf(COL_BOLD "  %-12s" COL_RESET " ", "Ping:");
    if (ping_result && ping_result->reachable) {
        printf(COL_GREEN "%.1f ms" COL_RESET "  ", ping_result->latency_ms);
        bar(ping_result->latency_ms, PING_SCALE_MAX, VISUAL_BAR_WIDTH,
         COL_GREEN);
        printf("\n");
    } else {
        printf(COL_RED "unreachable\n" COL_RESET);
    }

    /* TTFB */
    printf(COL_BOLD "  %-12s" COL_RESET " ", "TTFB:");
    if (conn_result && conn_result->success) {
        const char* col = COL_RED;
        if (conn_result->ttfb_ms < TTFB_GOOD_MS) {
            col = COL_GREEN;
        } else if (conn_result->ttfb_ms < TTFB_WARN_MS) {
            col = COL_YELLOW;
        }

        printf("%s%.0f ms" COL_RESET "  ", col, conn_result->ttfb_ms);
        bar(conn_result->ttfb_ms, TTFB_SCALE_MAX, VISUAL_BAR_WIDTH, col);
        printf("\n");
        printf(COL_BOLD "  %-12s" COL_RESET " %.0f ms  (HTTP %ld)\n",
         "Total:", conn_result->total_ms, conn_result->http_code);
    } else {
        printf(COL_RED "unavailable\n" COL_RESET);
    }

    if (result->download_mbps >= 0.0) {
        printf(COL_BOLD "  %-12s" COL_RESET COL_CYAN " %.1f Mbps\n" COL_RESET,
         "Download:", result->download_mbps);
    }
    if (result->upload_mbps >= 0.0) {
        printf(COL_BOLD
         "  %-12s" COL_RESET COL_MAGENTA " %.1f "
         "Mbps\n" COL_RESET,
         "Upload:", result->upload_mbps);
    }

    printf("\n");
}

#define DEFAULT_PING_COUNT 4
#define DEFAULT_FETCH_TIMEOUT 10

/* ------------------------------------------------------------------ */
/* run_provider                                                         */
/* ------------------------------------------------------------------ */

SpeedResult run_provider(Provider provider, const char* api_key)
{
    (void)api_key;
    SpeedResult result;
    memset(&result, 0, sizeof result);
    result.download_mbps = -1.0;
    result.upload_mbps = -1.0;
    result.ping_ms = -1.0;

    const ProviderDef* def = NULL;
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        if (PROVIDERS[i].id == provider) {
            def = &PROVIDERS[i];
            break;
        }
    }
    if (!def) {
        strncpy(result.provider, "unknown", sizeof result.provider - 1);
        return result;
    }

    printf(COL_BOLD COL_BLUE "\n[%s]" COL_RESET " Querying %s ...\n", def->name,
     def->url);

    /* 1. Connection quality metrics */
    ConnResult conn_result = measure_connection(def->url);

    printf("  Measuring download speed ... ");
    fflush(stdout);

    // Use large file endpoint if available
    const char* download_url = def->url;
    if (def->download_url_str != NULL) {
        download_url = def->download_url_str;
    }

    result.download_mbps =
     measure_download(download_url, def->download_headers);
    printf("%.2f Mbps\n", result.download_mbps);

    /* 2. Fetch body for parsing */
    char* body = curl_get(def->url, def->accept, DEFAULT_FETCH_TIMEOUT);

    /* 3. Parse */
    if (body) {
        result = def->parse(body);
        free(body);
    } else {
        strncpy(result.provider, def->name, sizeof result.provider - 1);
    }

    /* 4. TCP-ping */
    PingResult ping_result = ping_host(def->ping_host_str, DEFAULT_PING_COUNT);
    result.ping_ms = ping_result.reachable ? ping_result.latency_ms : -1.0;

    /* 5. Display */
    print_result(&result, &ping_result, &conn_result);

    return result;
}
