#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "libmcp.h"
#include "cJSON.h"
#include "sds.h"

#define HN_BASE_URL "https://hacker-news.firebaseio.com/v0"

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t realsize = size * nmemb;
    char** s = (char**)userp;
    size_t oldlen = *s ? strlen(*s) : 0;
    char* new_s = realloc(*s, oldlen + realsize + 1);
    if (!new_s) return 0;
    *s = new_s;
    memcpy(*s + oldlen, contents, realsize);
    (*s)[oldlen + realsize] = '\0';
    return realsize;
}

static cJSON* hn_get(const char* path)
{
    CURL* curl = NULL;
    char* response = NULL;

    while (path && *path == '/') path++;
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", HN_BASE_URL, path);

    curl = curl_easy_init();
    if (curl == NULL)
        goto fail;

    response = strdup("");
    if (response == NULL)
        goto fail;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        goto fail;

    cJSON* json = cJSON_Parse(response);
    curl_easy_cleanup(curl);
    free(response);
    return json;

fail:
    if (curl) curl_easy_cleanup(curl);
    if (response) free(response);
    return NULL;
}

int main(int argc, const char* argv[])
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    mcp_set_name("hackernews-mcp");
    mcp_set_version("1.0.0");

    fprintf(stderr, "HackerNews MCP Server running...\n");
    mcp_main(argc, argv);

    curl_global_cleanup();
    return 0;
}
