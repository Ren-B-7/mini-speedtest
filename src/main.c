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

#include "include/minicli.h"
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
/* CLI Callbacks                                                        */
/* ------------------------------------------------------------------ */

static int provider_cb(int argc, char** argv, void* user_data)
{
    SpeedtestConfig* config = (SpeedtestConfig*)user_data;
    if (argc < 1) {
        fprintf(stderr, "Error: Provider slug missing.\n");
        return 0;
    }
    config->chosen = provider_from_string(argv[0]);
    if (config->chosen == PROVIDER_UNKNOWN) {
        fprintf(stderr, "Unknown provider: %s\n", argv[0]);
        list_providers();
        exit(1);
    }
    return 1;
}

static int list_cb(int argc, char** argv, void* user_data)
{
    (void)argc;
    (void)argv;
    (void)user_data;
    list_providers();
    exit(0);
    return 0;
}

static int ping_only_cb(int argc, char** argv, void* user_data)
{
    (void)argc;
    (void)argv;
    SpeedtestConfig* config = (SpeedtestConfig*)user_data;
    config->ping_only = true;
    return 0;
}

static int json_cb(int argc, char** argv, void* user_data)
{
    (void)argc;
    (void)argv;
    SpeedtestConfig* config = (SpeedtestConfig*)user_data;
    config->json_mode = true;
    return 0;
}

static int count_cb(int argc, char** argv, void* user_data)
{
    SpeedtestConfig* config = (SpeedtestConfig*)user_data;
    if (argc < 1) {
        fprintf(stderr, "Error: Count missing.\n");
        return 0;
    }
    char* endptr;
    config->count = (int)strtol(argv[0], &endptr, 10);
    if (*endptr != '\0') {
        fprintf(stderr, "Invalid count: %s\n", argv[0]);
        exit(1);
    }
    if (config->count < 1) {
        config->count = 1;
    }
    if (config->count > MAX_PING_COUNT) {
        config->count = MAX_PING_COUNT;
    }
    return 1;
}

static int api_key_cb(int argc, char** argv, void* user_data)
{
    SpeedtestConfig* config = (SpeedtestConfig*)user_data;
    if (argc < 1) {
        fprintf(stderr, "Error: API key missing.\n");
        return 0;
    }
    config->api_key = argv[0];
    return 1;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char* argv[])
{
    SpeedtestConfig config = {
        PROVIDER_ALL, false, false, DEFAULT_PING_COUNT, NULL};

    CliParser parser;
    CliInitParams params = {"speedtest", "open-source network speed tester"};
    cli_init(&parser, params);

    cli_add_argument(&parser,
     (CliArgument){"--provider", "-p", "Provider to query (default: all)",
         provider_cb, &config});
    cli_add_argument(&parser,
     (CliArgument){
         "--list", "-l", "List available providers and exit", list_cb, NULL});
    cli_add_argument(&parser,
     (CliArgument){"--ping-only", NULL, "Only run TCP-connect ping test",
         ping_only_cb, &config});
    cli_add_argument(&parser,
     (CliArgument){"--json", "-j", "Output results as JSON", json_cb, &config});
    cli_add_argument(&parser,
     (CliArgument){"--count", "-c", "Ping probes per host (default: 4)",
         count_cb, &config});
    cli_add_argument(&parser,
     (CliArgument){
         "--api-key", "-k", "API key for providers", api_key_cb, &config});

    cli_parse(&parser, argc, argv);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (config.ping_only) {
        PingOptions opts = {config.chosen, config.count, config.json_mode};
        run_ping_only(opts);
        curl_global_cleanup();
        cli_destroy(&parser);
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
    cli_destroy(&parser);
    return 0;
}
