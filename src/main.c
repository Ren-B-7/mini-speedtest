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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speedtest.h"

#define COL_RESET "\033[0m"
#define COL_BOLD "\033[1m"
#define COL_CYAN "\033[36m"
#define COL_GREEN "\033[32m"

#define DEFAULT_PING_COUNT 4
#define MAX_PING_COUNT 20

/* ------------------------------------------------------------------ */
/* Types                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    Provider chosen;
    bool ping_only;
    bool json_mode;
    int count;
    const char* api_key;
} SpeedtestConfig;

typedef struct {
    const char* slug;
    const char* host;
} PingEntry;

typedef struct {
    Provider provider;
    int count;
    bool json_mode;
} PingOptions;

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

static const PingEntry PING_TABLE[] = {
    {"ipapi", "ip-api.com"},
    {"ipinfo", "ipinfo.io"},
    {"cloudflare", "one.one.one.one"},
    {"fastly", "api.fastly.com"},
    {"httpbin", "httpbin.org"},
};
static const int PING_TABLE_LEN =
 (int)(sizeof PING_TABLE / sizeof PING_TABLE[0]);

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
}

/* ------------------------------------------------------------------ */
/* JSON output helper                                                   */
/* ------------------------------------------------------------------ */

static void print_json_result(const SpeedResult* result,
 const PingResult* ping_result, const ConnResult* conn_result)
{
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "provider", result->provider);
    cJSON_AddStringToObject(obj, "ip", result->ip);
    cJSON_AddStringToObject(obj, "city", result->city);
    cJSON_AddStringToObject(obj, "region", result->region);
    cJSON_AddStringToObject(obj, "country", result->country);
    cJSON_AddStringToObject(obj, "org", result->org);
    cJSON_AddNumberToObject(obj, "latitude", result->latitude);
    cJSON_AddNumberToObject(obj, "longitude", result->longitude);

    if (ping_result) {
        cJSON_AddBoolToObject(obj, "ping_reachable", ping_result->reachable);
        cJSON_AddNumberToObject(obj, "ping_ms",
         ping_result->reachable ? ping_result->latency_ms : -1.0);
    }
    if (conn_result) {
        cJSON_AddBoolToObject(obj, "conn_success", conn_result->success);
        cJSON_AddNumberToObject(obj, "ttfb_ms",
         conn_result->success ? conn_result->ttfb_ms : -1.0);
        cJSON_AddNumberToObject(obj, "total_ms",
         conn_result->success ? conn_result->total_ms : -1.0);
        cJSON_AddNumberToObject(obj, "http_code",
         (double)conn_result->http_code);
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

static void run_ping_only(PingOptions opts)
{
    for (int i = 0; i < PING_TABLE_LEN; i++) {
        Provider pid = provider_from_string(PING_TABLE[i].slug);
        if (opts.provider != PROVIDER_ALL && pid != opts.provider) {
            continue;
        }

        printf(COL_BOLD "[ping] " COL_RESET "%s ... ", PING_TABLE[i].host);
        fflush(stdout);

        PingResult ping_result = ping_host(PING_TABLE[i].host, opts.count);

        if (opts.json_mode) {
            printf("\n");
            cJSON* obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "host", PING_TABLE[i].host);
            cJSON_AddBoolToObject(obj, "reachable", ping_result.reachable);
            cJSON_AddNumberToObject(obj, "latency_ms",
             ping_result.reachable ? ping_result.latency_ms : -1.0);
            char* out = cJSON_Print(obj);
            if (out) {
                puts(out);
                free(out);
            }
            cJSON_Delete(obj);
        } else {
            if (ping_result.reachable) {
                printf(COL_GREEN "%.1f ms\n" COL_RESET, ping_result.latency_ms);
            } else {
                printf("\033[31munreachable\n" COL_RESET);
            }
        }
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* Argument Parsing                                                     */
/* ------------------------------------------------------------------ */

static int parse_args(int argc, char* argv[], SpeedtestConfig* config)
{
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(argv[0]);
            return 0; // Exit successfully
        }
        if (strcmp(arg, "--list") == 0) {
            list_providers();
            return 0; // Exit successfully
        }
        if (strcmp(arg, "--ping-only") == 0) {
            config->ping_only = true;
            continue;
        }
        if (strcmp(arg, "--json") == 0) {
            config->json_mode = true;
            continue;
        }

        if (strncmp(arg, "--provider=", 11) == 0) {
            const char* slug = arg + 11;
            config->chosen = provider_from_string(slug);
            if (config->chosen == PROVIDER_UNKNOWN) {
                fprintf(stderr, "Unknown provider: %s\n", slug);
                list_providers();
                return -1;
            }
            continue;
        }
        if (strncmp(arg, "--count=", 8) == 0) {
            char* endptr;
            config->count = (int)strtol(arg + 8, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "Invalid count: %s\n", arg + 8);
                return -1;
            }
            if (config->count < 1) {
                config->count = 1;
            }
            if (config->count > MAX_PING_COUNT) {
                config->count = MAX_PING_COUNT;
            }
            continue;
        }
        if (strncmp(arg, "--api-key=", 10) == 0) {
            config->api_key = arg + 10;
            continue;
        }

        fprintf(stderr, "Unknown option: %s\n", arg);
        usage(argv[0]);
        return -1;
    }
    return 1; // Continue execution
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char* argv[])
{
    SpeedtestConfig config = {
        PROVIDER_ALL, false, false, DEFAULT_PING_COUNT, NULL};

    int status = parse_args(argc, argv, &config);
    if (status <= 0) {
        return (status == 0) ? 0 : 1;
    }

    if (!config.json_mode) {
        printf(COL_BOLD
         "\n+----------------------------------+\n"
         "|      speedtest-cli  v1.0         |\n"
         "+----------------------------------+\n" COL_RESET);
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (config.ping_only) {
        PingOptions opts = {config.chosen, config.count, config.json_mode};
        run_ping_only(opts);
        curl_global_cleanup();
        return 0;
    }

    if (config.chosen == PROVIDER_ALL) {
        for (int p = PROVIDER_IPAPI; p < PROVIDER_ALL; p++) {
            SpeedResult result = run_provider((Provider)p, config.api_key);
            if (config.json_mode) {
                PingResult ping_result;
                memset(&ping_result, 0, sizeof ping_result);
                ConnResult conn_result;
                memset(&conn_result, 0, sizeof conn_result);
                print_json_result(&result, &ping_result, &conn_result);
            }
        }
    } else {
        SpeedResult result = run_provider(config.chosen, config.api_key);
        if (config.json_mode) {
            PingResult ping_result;
            memset(&ping_result, 0, sizeof ping_result);
            ConnResult conn_result;
            memset(&conn_result, 0, sizeof conn_result);
            print_json_result(&result, &ping_result, &conn_result);
        }
    }

    curl_global_cleanup();
    return 0;
}
