/*
 * json_parse.c  -  Provider-specific response parsers
 *
 * Each function accepts the raw response body (string) returned by
 * libcurl and fills a SpeedResult.  Parsing is done with cJSON.
 *
 * Provider formats:
 *   ip-api.com   -> JSON object with lat/lon/org/city/...
 *   ipinfo.io    -> JSON with "loc":"lat,lon" string
 *   Cloudflare   -> plain key=value text ("trace" endpoint)
 *   Fastly       -> JSON from their free CDN-info API
 *   httpbin.org  -> JSON echo (we read "origin" for the IP)
 */

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speedtest.h"

#define POP_SUFFIX_LEN 7
#define CLOUDFLARE_KEY_MAX 64
#define CLOUDFLARE_VAL_MAX 256

/* ------------------------------------------------------------------ */
/* utility                                                              */
/* ------------------------------------------------------------------ */

static void safe_str(char* dst, size_t dstsz, const cJSON* item)
{
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, dstsz - 1);
        dst[dstsz - 1] = '\0';
    }
}

static void safe_str_pop(char* dst, size_t dstsz, const char* src)
{
    if (!src) {
        return;
    }

    // Use a temporary buffer to avoid truncation warnings
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s (PoP)", src);
    strncpy(dst, tmp, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

static double safe_num(const cJSON* item, double fallback)
{
    if (cJSON_IsNumber(item)) {
        return item->valuedouble;
    }
    return fallback;
}

/* ------------------------------------------------------------------ */
/* ip-api.com                                                           */
/* ------------------------------------------------------------------ */
SpeedResult parse_ipapi(const char* json)
{
    SpeedResult result;
    memset(&result, 0, sizeof result);
    strncpy(result.provider, "ip-api.com", sizeof result.provider - 1);
    result.download_mbps = -1.0;
    result.upload_mbps = -1.0;
    result.ping_ms = -1.0;

    if (!json) {
        return result;
    }

    cJSON* json_root = cJSON_Parse(json);
    if (!json_root) {
        return result;
    }

    cJSON* status = cJSON_GetObjectItemCaseSensitive(json_root, "status");
    if (!cJSON_IsString(status) ||
     strcmp(status->valuestring, "success") != 0) {
        cJSON_Delete(json_root);
        return result;
    }

    safe_str(result.ip, sizeof result.ip,
     cJSON_GetObjectItem(json_root, "query"));
    safe_str(result.city, sizeof result.city,
     cJSON_GetObjectItem(json_root, "city"));
    safe_str(result.region, sizeof result.region,
     cJSON_GetObjectItem(json_root, "regionName"));
    safe_str(result.country, sizeof result.country,
     cJSON_GetObjectItem(json_root, "countryCode"));
    safe_str(result.org, sizeof result.org,
     cJSON_GetObjectItem(json_root, "isp"));

    result.latitude = safe_num(cJSON_GetObjectItem(json_root, "lat"), 0.0);
    result.longitude = safe_num(cJSON_GetObjectItem(json_root, "lon"), 0.0);
    result.valid = 1;

    cJSON_Delete(json_root);
    return result;
}

/* ------------------------------------------------------------------ */
/* ipinfo.io                                                            */
/* ------------------------------------------------------------------ */
SpeedResult parse_ipinfo(const char* json)
{
    SpeedResult result;
    memset(&result, 0, sizeof result);
    strncpy(result.provider, "ipinfo.io", sizeof result.provider - 1);
    result.download_mbps = -1.0;
    result.upload_mbps = -1.0;
    result.ping_ms = -1.0;

    if (!json) {
        return result;
    }

    cJSON* json_root = cJSON_Parse(json);
    if (!json_root) {
        return result;
    }

    safe_str(result.ip, sizeof result.ip, cJSON_GetObjectItem(json_root, "ip"));
    safe_str(result.city, sizeof result.city,
     cJSON_GetObjectItem(json_root, "city"));
    safe_str(result.region, sizeof result.region,
     cJSON_GetObjectItem(json_root, "region"));
    safe_str(result.country, sizeof result.country,
     cJSON_GetObjectItem(json_root, "country"));
    safe_str(result.org, sizeof result.org,
     cJSON_GetObjectItem(json_root, "org"));

    /* "loc" is "lat,lon" as a single string */
    cJSON* loc = cJSON_GetObjectItem(json_root, "loc");
    if (cJSON_IsString(loc) && loc->valuestring) {
        char* endptr;
        result.latitude = strtod(loc->valuestring, &endptr);
        if (endptr != loc->valuestring && *endptr == ',') {
            result.longitude = strtod(endptr + 1, NULL);
        }
    }

    result.valid = (result.ip[0] != '\0');

    cJSON_Delete(json_root);
    return result;
}

/* ------------------------------------------------------------------ */
/* Cloudflare trace (plain text key=value)                             */
/* ------------------------------------------------------------------ */
SpeedResult parse_cloudflare(const char* raw)
{
    SpeedResult result;
    memset(&result, 0, sizeof result);
    strncpy(result.provider, "Cloudflare", sizeof result.provider - 1);
    result.download_mbps = -1.0;
    result.upload_mbps = -1.0;
    result.ping_ms = -1.0;

    if (!raw) {
        return result;
    }

    char* buf = strdup(raw);
    if (!buf) {
        return result;
    }

    char* line = strtok(buf, "\n");
    while (line) {
        char key[CLOUDFLARE_KEY_MAX] = {0};
        char val[CLOUDFLARE_VAL_MAX] = {0};
        if (sscanf(line, "%63[^=]=%255s", key, val) == 2) {
            if (strcmp(key, "ip") == 0) {
                strncpy(result.ip, val, sizeof result.ip - 1);
                result.ip[sizeof result.ip - 1] = '\0';
            } else if (strcmp(key, "loc") == 0) {
                strncpy(result.country, val, sizeof result.country - 1);
                result.country[sizeof result.country - 1] = '\0';
            } else if (strcmp(key, "colo") == 0) {
                safe_str_pop(result.city, sizeof result.city, val);
            } else if (strcmp(key, "org") == 0) {
                strncpy(result.org, val, sizeof result.org - 1);
                result.org[sizeof result.org - 1] = '\0';
            }
        }
        line = strtok(NULL, "\n");
    }

    free(buf);
    result.valid = (result.ip[0] != '\0');
    return result;
}

/* ------------------------------------------------------------------ */
/* Fastly edge probe                                                    */
/* ------------------------------------------------------------------ */
SpeedResult parse_fastly(const char* json)
{
    SpeedResult result;
    memset(&result, 0, sizeof result);
    strncpy(result.provider, "Fastly", sizeof result.provider - 1);
    result.download_mbps = -1.0;
    result.upload_mbps = -1.0;
    result.ping_ms = -1.0;

    if (!json) {
        return result;
    }

    cJSON* json_root = cJSON_Parse(json);
    if (!json_root) {
        return result;
    }

    safe_str(result.ip, sizeof result.ip,
     cJSON_GetObjectItem(json_root, "client_ip"));

    cJSON* geo = cJSON_GetObjectItem(json_root, "pop_geolocation");
    if (geo) {
        safe_str(result.city, sizeof result.city,
         cJSON_GetObjectItem(geo, "city"));
        safe_str(result.country, sizeof result.country,
         cJSON_GetObjectItem(geo, "country"));
        result.latitude = safe_num(cJSON_GetObjectItem(geo, "latitude"), 0.0);
        result.longitude = safe_num(cJSON_GetObjectItem(geo, "longitude"), 0.0);
    }

    cJSON* asn = cJSON_GetObjectItem(json_root, "as_number");
    if (cJSON_IsNumber(asn)) {
        snprintf(result.org, sizeof result.org, "AS%d", (int)asn->valuedouble);
    }

    result.valid = (result.ip[0] != '\0');
    cJSON_Delete(json_root);
    return result;
}

/* ------------------------------------------------------------------ */
/* httpbin.org                                                          */
/* ------------------------------------------------------------------ */
SpeedResult parse_httpbin(const char* json)
{
    SpeedResult result;
    memset(&result, 0, sizeof result);
    strncpy(result.provider, "httpbin.org", sizeof result.provider - 1);
    result.download_mbps = -1.0;
    result.upload_mbps = -1.0;
    result.ping_ms = -1.0;

    if (!json) {
        return result;
    }

    cJSON* json_root = cJSON_Parse(json);
    if (!json_root) {
        return result;
    }

    cJSON* origin = cJSON_GetObjectItem(json_root, "origin");
    if (cJSON_IsString(origin) && origin->valuestring) {
        strncpy(result.ip, origin->valuestring, sizeof result.ip - 1);
        result.ip[sizeof result.ip - 1] = '\0';
        char* comma = strchr(result.ip, ',');
        if (comma) {
            *comma = '\0';
        }
    }

    snprintf(result.org, sizeof result.org, "echo test (no geo)");

    result.valid = (result.ip[0] != '\0');
    cJSON_Delete(json_root);
    return result;
}
