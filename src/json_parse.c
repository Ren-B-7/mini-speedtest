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

    // Check if we have space for " (PoP)\0" which is 7 bytes
    if (strlen(src) + 7 <= dstsz) {
        snprintf(dst, dstsz, "%s (PoP)", src);
    } else {
        strncpy(dst, src, dstsz - 1);
        dst[dstsz - 1] = '\0';
    }
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
    SpeedResult r;
    memset(&r, 0, sizeof r);
    strncpy(r.provider, "ip-api.com", sizeof r.provider - 1);
    r.download_mbps = -1.0;
    r.upload_mbps = -1.0;
    r.ping_ms = -1.0;

    if (!json) {
        return r;
    }

    cJSON* root = cJSON_Parse(json);
    if (!root) {
        return r;
    }

    cJSON* status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsString(status) ||
     strcmp(status->valuestring, "success") != 0) {
        cJSON_Delete(root);
        return r;
    }

    safe_str(r.ip, sizeof r.ip, cJSON_GetObjectItem(root, "query"));
    safe_str(r.city, sizeof r.city, cJSON_GetObjectItem(root, "city"));
    safe_str(r.region, sizeof r.region,
     cJSON_GetObjectItem(root, "regionName"));
    safe_str(r.country, sizeof r.country,
     cJSON_GetObjectItem(root, "countryCode"));
    safe_str(r.org, sizeof r.org, cJSON_GetObjectItem(root, "isp"));

    r.latitude = safe_num(cJSON_GetObjectItem(root, "lat"), 0.0);
    r.longitude = safe_num(cJSON_GetObjectItem(root, "lon"), 0.0);
    r.valid = 1;

    cJSON_Delete(root);
    return r;
}

/* ------------------------------------------------------------------ */
/* ipinfo.io                                                            */
/* ------------------------------------------------------------------ */
SpeedResult parse_ipinfo(const char* json)
{
    SpeedResult r;
    memset(&r, 0, sizeof r);
    strncpy(r.provider, "ipinfo.io", sizeof r.provider - 1);
    r.download_mbps = -1.0;
    r.upload_mbps = -1.0;
    r.ping_ms = -1.0;

    if (!json) {
        return r;
    }

    cJSON* root = cJSON_Parse(json);
    if (!root) {
        return r;
    }

    safe_str(r.ip, sizeof r.ip, cJSON_GetObjectItem(root, "ip"));
    safe_str(r.city, sizeof r.city, cJSON_GetObjectItem(root, "city"));
    safe_str(r.region, sizeof r.region, cJSON_GetObjectItem(root, "region"));
    safe_str(r.country, sizeof r.country, cJSON_GetObjectItem(root, "country"));
    safe_str(r.org, sizeof r.org, cJSON_GetObjectItem(root, "org"));

    /* "loc" is "lat,lon" as a single string */
    cJSON* loc = cJSON_GetObjectItem(root, "loc");
    if (cJSON_IsString(loc) && loc->valuestring) {
        char* endptr;
        r.latitude = strtod(loc->valuestring, &endptr);
        if (endptr != loc->valuestring && *endptr == ',') {
            r.longitude = strtod(endptr + 1, NULL);
        }
    }

    r.valid = (r.ip[0] != '\0');

    cJSON_Delete(root);
    return r;
}

/* ------------------------------------------------------------------ */
/* Cloudflare trace (plain text key=value)                             */
/* ------------------------------------------------------------------ */
SpeedResult parse_cloudflare(const char* raw)
{
    SpeedResult r;
    memset(&r, 0, sizeof r);
    strncpy(r.provider, "Cloudflare", sizeof r.provider - 1);
    r.download_mbps = -1.0;
    r.upload_mbps = -1.0;
    r.ping_ms = -1.0;

    if (!raw) {
        return r;
    }

    char* buf = strdup(raw);
    if (!buf) {
        return r;
    }

    char* line = strtok(buf, "\n");
    while (line) {
        char key[64] = {0}, val[256] = {0};
        if (sscanf(line, "%63[^=]=%255s", key, val) == 2) {
            if (strcmp(key, "ip") == 0) {
                strncpy(r.ip, val, sizeof r.ip - 1);
                r.ip[sizeof r.ip - 1] = '\0';
            } else if (strcmp(key, "loc") == 0) {
                strncpy(r.country, val, sizeof r.country - 1);
                r.country[sizeof r.country - 1] = '\0';
            } else if (strcmp(key, "colo") == 0) {
                safe_str_pop(r.city, sizeof r.city, val);
            } else if (strcmp(key, "org") == 0) {
                strncpy(r.org, val, sizeof r.org - 1);
                r.org[sizeof r.org - 1] = '\0';
            }
        }
        line = strtok(NULL, "\n");
    }

    free(buf);
    r.valid = (r.ip[0] != '\0');
    return r;
}

/* ------------------------------------------------------------------ */
/* Fastly edge probe                                                    */
/* ------------------------------------------------------------------ */
SpeedResult parse_fastly(const char* json)
{
    SpeedResult r;
    memset(&r, 0, sizeof r);
    strncpy(r.provider, "Fastly", sizeof r.provider - 1);
    r.download_mbps = -1.0;
    r.upload_mbps = -1.0;
    r.ping_ms = -1.0;

    if (!json) {
        return r;
    }

    cJSON* root = cJSON_Parse(json);
    if (!root) {
        return r;
    }

    safe_str(r.ip, sizeof r.ip, cJSON_GetObjectItem(root, "client_ip"));

    cJSON* geo = cJSON_GetObjectItem(root, "pop_geolocation");
    if (geo) {
        safe_str(r.city, sizeof r.city, cJSON_GetObjectItem(geo, "city"));
        safe_str(r.country, sizeof r.country,
         cJSON_GetObjectItem(geo, "country"));
        r.latitude = safe_num(cJSON_GetObjectItem(geo, "latitude"), 0.0);
        r.longitude = safe_num(cJSON_GetObjectItem(geo, "longitude"), 0.0);
    }

    cJSON* asn = cJSON_GetObjectItem(root, "as_number");
    if (cJSON_IsNumber(asn)) {
        snprintf(r.org, sizeof r.org, "AS%d", (int)asn->valuedouble);
    }

    r.valid = (r.ip[0] != '\0');
    cJSON_Delete(root);
    return r;
}

/* ------------------------------------------------------------------ */
/* httpbin.org                                                          */
/* ------------------------------------------------------------------ */
SpeedResult parse_httpbin(const char* json)
{
    SpeedResult r;
    memset(&r, 0, sizeof r);
    strncpy(r.provider, "httpbin.org", sizeof r.provider - 1);
    r.download_mbps = -1.0;
    r.upload_mbps = -1.0;
    r.ping_ms = -1.0;

    if (!json) {
        return r;
    }

    cJSON* root = cJSON_Parse(json);
    if (!root) {
        return r;
    }

    cJSON* origin = cJSON_GetObjectItem(root, "origin");
    if (cJSON_IsString(origin) && origin->valuestring) {
        strncpy(r.ip, origin->valuestring, sizeof r.ip - 1);
        r.ip[sizeof r.ip - 1] = '\0';
        char* comma = strchr(r.ip, ',');
        if (comma) {
            *comma = '\0';
        }
    }

    snprintf(r.org, sizeof r.org, "echo test (no geo)");

    r.valid = (r.ip[0] != '\0');
    cJSON_Delete(root);
    return r;
}
