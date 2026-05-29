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

#define MAX_DOWNLOAD_SIZE ((size_t)15 * 1024 * 1024)

/* Download callback to discard data but count bytes */
static size_t download_cb(void* ptr, size_t size, size_t nmemb, void* user_data)
{
    size_t bytes = size * nmemb;
    size_t* total_bytes = (size_t*)user_data;
    *total_bytes += bytes;
    (void)ptr;

    // Stop downloading if we've reached 15MB
    if (*total_bytes >= MAX_DOWNLOAD_SIZE) {
        return 0; // Returning 0 aborts the transfer
    }

    return bytes;
}

/* Discard response body – we only care about timing */
static size_t discard_cb(void* ptr, size_t size, size_t nmemb, void* user_data)
{
    (void)ptr;
    (void)user_data;
    return size * nmemb;
}

/* ── public API ─────────────────────────────────────────────────────── */

double measure_download(const char* url, const char* const* extra_headers)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        return -1.0;
    }

    size_t total_bytes = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &total_bytes);

    // Set a 10s hard timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "speedtest-cli/1.0");

    struct curl_slist* hdrs = NULL;
    if (extra_headers) {
        for (const char* const* h = extra_headers; *h; h++) {
            hdrs = curl_slist_append(hdrs, *h);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    }

    double start = 0.0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    start = (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);

    CURLcode res = curl_easy_perform(curl);

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);
    double end = (double)tv_end.tv_sec + ((double)tv_end.tv_usec / 1000000.0);
    double duration = end - start;

    if (hdrs) {
        curl_slist_free_all(hdrs);
    }
    curl_easy_cleanup(curl);

    // Accept CURLE_WRITE_ERROR because it's how we abort early
    if ((res != CURLE_OK && res != CURLE_WRITE_ERROR) || duration <= 0) {
        return -1.0;
    }

    // Bytes to Megabits: (bytes * 8) / (duration * 1000000)
    return ((double)total_bytes * 8.0) / (duration * 1000000.0);
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
