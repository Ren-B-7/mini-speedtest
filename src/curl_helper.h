#ifndef CURL_HELPER_H
#define CURL_HELPER_H

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

/* Dynamic write buffer filled by libcurl */
typedef struct {
    char* data;
    size_t len;
} CurlBuf;

/* Write callback - pass as CURLOPT_WRITEFUNCTION */
static size_t
curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata)
{
    CurlBuf* buf = (CurlBuf*)userdata;
    size_t new_bytes = size * nmemb;
    char* tmp = realloc(buf->data, buf->len + new_bytes + 1);
    if (!tmp) {
        return 0;
    }
    buf->data = tmp;
    memcpy(buf->data + buf->len, ptr, new_bytes);
    buf->len += new_bytes;
    buf->data[buf->len] = '\0';
    return new_bytes;
}

/*
 * Perform a simple GET and return an allocated string (caller frees).
 * Returns NULL on failure.
 */
static inline char*
curl_get(const char* url, const char* accept_header, long timeout_s)
{
    CURL* curl;
    CURLcode res;
    CurlBuf buf = {NULL, 0};

    curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }

    struct curl_slist* hdrs = NULL;
    if (accept_header) {
        hdrs = curl_slist_append(hdrs, accept_header);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s > 0 ? timeout_s : 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "speedtest-cli/1.0");
    if (hdrs) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    }

    res = curl_easy_perform(curl);

    if (hdrs) {
        curl_slist_free_all(hdrs);
    }
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

#endif /* CURL_HELPER_H */
