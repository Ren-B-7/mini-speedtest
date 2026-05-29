/*
 * main.c  -  speedtest-cli entry point
 *
 * Usage:
 *   speedtest [--provider=<slug>] [--list] [--ping-only] [--json]
 *             [--count=<n>] [--help]
 *
 * Providers: ipapi, ipinfo, cloudflare, fastly, httpbin, all
 */

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speedtest.h"

#define COL_RESET "\033[0m"
#define COL_BOLD "\033[1m"
#define COL_CYAN "\033[36m"
#define COL_GREEN "\033[32m"

/* ------------------------------------------------------------------ */
/* Help                                                                 */
/* ------------------------------------------------------------------ */

static void usage(const char* argv0)
{
    printf(
     "\n" COL_BOLD "speedtest-cli" COL_RESET " - open-source network speed "
     "tester\n"
     "\n"
     "Usage:\n"
     "  %s [options]\n"
     "\n"
     "Options:\n"
     "  " COL_CYAN "--provider=<slug>" COL_RESET "   Provider to query  "
     "(default: all)\n"
     "  " COL_CYAN "--list" COL_RESET "               List available providers "
     "and exit\n"
     "  " COL_CYAN "--ping-only" COL_RESET "          Only run TCP-connect "
     "ping test\n"
     "  " COL_CYAN "--json" COL_RESET "               Output results as JSON\n"
     "  " COL_CYAN "--count=<n>" COL_RESET "          Ping probes per host "
     "(default: 4)\n"
     "  " COL_CYAN "--api-key=<key>" COL_RESET "      API key for providers\n"
     "  " COL_CYAN "--help" COL_RESET "               Show this help\n"
     "\n",
     argv0);
    list_providers();
}

/* ------------------------------------------------------------------ */
/* JSON output helper                                                   */
/* ------------------------------------------------------------------ */

static void print_json_result(const SpeedResult* r, const PingResult* pr,
 const ConnResult* cr)
{
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "provider", r->provider);
    cJSON_AddStringToObject(obj, "ip", r->ip);
    cJSON_AddStringToObject(obj, "city", r->city);
    cJSON_AddStringToObject(obj, "region", r->region);
    cJSON_AddStringToObject(obj, "country", r->country);
    cJSON_AddStringToObject(obj, "org", r->org);
    cJSON_AddNumberToObject(obj, "latitude", r->latitude);
    cJSON_AddNumberToObject(obj, "longitude", r->longitude);

    if (pr) {
        cJSON_AddBoolToObject(obj, "ping_reachable", pr->reachable);
        cJSON_AddNumberToObject(obj, "ping_ms",
         pr->reachable ? pr->latency_ms : -1.0);
    }
    if (cr) {
        cJSON_AddBoolToObject(obj, "conn_success", cr->success);
        cJSON_AddNumberToObject(obj, "ttfb_ms",
         cr->success ? cr->ttfb_ms : -1.0);
        cJSON_AddNumberToObject(obj, "total_ms",
         cr->success ? cr->total_ms : -1.0);
        cJSON_AddNumberToObject(obj, "http_code", (double)cr->http_code);
    }

    char* out = cJSON_Print(obj);
    if (out) {
        puts(out);
        free(out);
    }
    cJSON_Delete(obj);
}

/* ------------------------------------------------------------------ */
/* Ping-only mode                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    const char* slug;
    const char* host;
} PingEntry;

static const PingEntry PING_TABLE[] = {
    {"ipapi", "ip-api.com"},
    {"ipinfo", "ipinfo.io"},
    {"cloudflare", "one.one.one.one"},
    {"fastly", "api.fastly.com"},
    {"httpbin", "httpbin.org"},
};
static const int PING_TABLE_LEN =
 (int)(sizeof PING_TABLE / sizeof PING_TABLE[0]);

static void run_ping_only(Provider p, int count, int json_mode)
{
    for (int i = 0; i < PING_TABLE_LEN; i++) {
        Provider pid = provider_from_string(PING_TABLE[i].slug);
        if (p != PROVIDER_ALL && pid != p) {
            continue;
        }

        printf(COL_BOLD "[ping] " COL_RESET "%s ... ", PING_TABLE[i].host);
        fflush(stdout);

        PingResult pr = ping_host(PING_TABLE[i].host, count);

        if (json_mode) {
            printf("\n");
            cJSON* obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "host", PING_TABLE[i].host);
            cJSON_AddBoolToObject(obj, "reachable", pr.reachable);
            cJSON_AddNumberToObject(obj, "latency_ms",
             pr.reachable ? pr.latency_ms : -1.0);
            char* out = cJSON_Print(obj);
            if (out) {
                puts(out);
                free(out);
            }
            cJSON_Delete(obj);
        } else {
            if (pr.reachable) {
                printf(COL_GREEN "%.1f ms\n" COL_RESET, pr.latency_ms);
            } else {
                printf("\033[31munreachable\n" COL_RESET);
            }
        }
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char* argv[])
{
    Provider chosen = PROVIDER_ALL;
    int ping_only = 0;
    int json_mode = 0;
    int count = 4;
    const char* api_key = NULL;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(arg, "--list") == 0) {
            list_providers();
            return 0;
        }
        if (strcmp(arg, "--ping-only") == 0) {
            ping_only = 1;
            continue;
        }
        if (strcmp(arg, "--json") == 0) {
            json_mode = 1;
            continue;
        }

        if (strncmp(arg, "--provider=", 11) == 0) {
            const char* slug = arg + 11;
            chosen = provider_from_string(slug);
            if (chosen == PROVIDER_UNKNOWN) {
                fprintf(stderr, "Unknown provider: %s\n", slug);
                list_providers();
                return 1;
            }
            continue;
        }
        if (strncmp(arg, "--count=", 8) == 0) {
            char* endptr;
            count = (int)strtol(arg + 8, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "Invalid count: %s\n", arg + 8);
                return 1;
            }
            if (count < 1) {
                count = 1;
            }
            if (count > 20) {
                count = 20;
            }
            continue;
        }
        if (strncmp(arg, "--api-key=", 10) == 0) {
            api_key = arg + 10;
            continue;
        }

        fprintf(stderr, "Unknown option: %s\n", arg);
        usage(argv[0]);
        return 1;
    }

    if (!json_mode) {
        printf(COL_BOLD
         "\n+----------------------------------+\n"
         "|      speedtest-cli  v1.0         |\n"
         "+----------------------------------+\n" COL_RESET);
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (ping_only) {
        run_ping_only(chosen, count, json_mode);
        curl_global_cleanup();
        return 0;
    }

    if (chosen == PROVIDER_ALL) {
        for (int p = PROVIDER_IPAPI; p < PROVIDER_ALL; p++) {
            SpeedResult r = run_provider((Provider)p, api_key);
            if (json_mode) {
                PingResult pr;
                memset(&pr, 0, sizeof pr);
                ConnResult cr;
                memset(&cr, 0, sizeof cr);
                print_json_result(&r, &pr, &cr);
            }
        }
    } else {
        SpeedResult r = run_provider(chosen, api_key);
        if (json_mode) {
            PingResult pr;
            memset(&pr, 0, sizeof pr);
            ConnResult cr;
            memset(&cr, 0, sizeof cr);
            print_json_result(&r, &pr, &cr);
        }
    }

    curl_global_cleanup();
    return 0;
}
