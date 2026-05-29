/*
 * connection.c  –  HTTP connection quality metrics
 *
 * Uses libcurl's built-in timing infrastructure to report:
 *   • TTFB  (time-to-first-byte)  = namelookup + connect + TLS + pretransfer
 *   • Total = full transfer time
 *   • HTTP status code
 *
 * These numbers are complementary to the TCP-connect ping in ping.c.
 * TTFB captures DNS + TLS overhead which pure TCP misses.
 */

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speedtest.h"


/* Download callback to discard data but count bytes */
static size_t download_cb(void* ptr, size_t size, size_t nmemb, void* ud)
{
    size_t bytes = size * nmemb;
    size_t* total_bytes = (size_t*)ud;
    *total_bytes += bytes;
    (void)ptr;
    return bytes;
}

/* Discard response body – we only care about timing */
static size_t discard_cb(void* ptr, size_t size, size_t nmemb, void* ud)
{
    (void)ptr;
    (void)ud;
    return size * nmemb;
}

/* ── public API ─────────────────────────────────────────────────────── */

double measure_download(const char* url)
{
    CURL* curl = curl_easy_init();
    if (!curl) return -1.0;

    size_t total_bytes = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &total_bytes);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5s test
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "speedtest-cli/1.0");

    double start = 0.0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    start = tv.tv_sec + (tv.tv_usec / 1000000.0);

    CURLcode res = curl_easy_perform(curl);
    
    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);
    double end = tv_end.tv_sec + (tv_end.tv_usec / 1000000.0);
    double duration = end - start;

    curl_easy_cleanup(curl);

    if (res != CURLE_OK || duration <= 0) return -1.0;

    // Bytes to Megabits: (bytes * 8) / (duration * 1000000)
    return (total_bytes * 8.0) / (duration * 1000000.0);
}

ConnResult measure_connection(const char* url)
{
    ConnResult result;
    memset(&result, 0, sizeof result);
    // ... existing implementation ...

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.success = 0;
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_cb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "speedtest-cli/1.0");
    /* Allow HTTP/2 where available */
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        double namelookup, connect_t, pretransfer, starttransfer, total;

        curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &namelookup);
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_t);
        curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME, &pretransfer);
        curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &starttransfer);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.http_code);

        /* TTFB = time from start until first response byte arrived */
        result.ttfb_ms = starttransfer * 1000.0;
        result.total_ms = total * 1000.0;
        result.success = 1;

        (void)namelookup;
        (void)connect_t;
        (void)pretransfer;
    } else {
        result.success = 0;
    }

    curl_easy_cleanup(curl);
    return result;
}
