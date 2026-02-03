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

static McpToolCallResult* fetch_stories(const char* endpoint, int limit)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* ids_json = hn_get(endpoint);
    if (!ids_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch story IDs from HackerNews");
        return r;
    }

    if (!cJSON_IsArray(ids_json)) {
        cJSON_Delete(ids_json);
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Invalid response: expected array of story IDs");
        return r;
    }

    sds result = sdsempty();

    int count = 0;
    cJSON* id_item = NULL;
    cJSON_ArrayForEach(id_item, ids_json) {
        if (count >= limit) break;
        if (!cJSON_IsNumber(id_item)) continue;

        int story_id = id_item->valueint;
        char path[128];
        snprintf(path, sizeof(path), "item/%d.json", story_id);

        cJSON* story_json = hn_get(path);
        if (!story_json) continue;

        cJSON* id = cJSON_Select(story_json, ".id:n");
        cJSON* title = cJSON_Select(story_json, ".title:s");
        cJSON* by = cJSON_Select(story_json, ".by:s");
        cJSON* score = cJSON_Select(story_json, ".score:n");
        cJSON* url = cJSON_Select(story_json, ".url:s");
        cJSON* time = cJSON_Select(story_json, ".time:n");

        if (id && title) {
            result = sdscatprintf(result, "#%d: %s\n", id->valueint, title->valuestring);
            if (by)
                result = sdscatprintf(result, "  Author: %s\n", by->valuestring);
            if (score)
                result = sdscatprintf(result, "  Score: %d points\n", score->valueint);
            if (url)
                result = sdscatprintf(result, "  URL: %s\n", url->valuestring);
            if (time) {
                time_t timestamp = time->valuedouble;
                char time_str[64];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", gmtime(&timestamp));
                result = sdscatprintf(result, "  Time: %s UTC\n", time_str);
            }
            result = sdscat(result, "\n");
            count++;
        }

        cJSON_Delete(story_json);
    }

    if (count == 0) {
        result = sdscat(result, "No stories found\n");
    }

    cJSON_Delete(ids_json);

    mcp_tool_call_result_add_text(r, result);
    sdsfree(result);
    return r;
}

static McpToolCallResult* get_max_item_handler(cJSON* params)
{
    (void)params;

    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* json = hn_get("maxitem.json");
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch max item from HackerNews");
        return r;
    }

    sds result = sdsempty();
    if (cJSON_IsNumber(json)) {
        result = sdscatprintf(result, "Current max item ID: %d\n", json->valueint);
    } else {
        result = sdscat(result, "Unexpected response format\n");
    }

    cJSON_Delete(json);

    mcp_tool_call_result_add_text(r, result);
    sdsfree(result);
    return r;
}

static McpInputSchema tool_get_max_item_schema[] = {
    mcp_input_schema_null
};

static McpTool tool_get_max_item = {
    .name = "get_max_item",
    .description = "Get the current largest item ID on HackerNews",
    .handler = get_max_item_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_get_max_item_schema,
    },
};

static McpToolCallResult* get_updates_handler(cJSON* params)
{
    (void)params;

    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* json = hn_get("updates.json");
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch updates from HackerNews");
        return r;
    }

    sds result = sdsempty();

    cJSON* items = cJSON_Select(json, ".items:a");
    if (items) {
        result = sdscat(result, "Recent item changes:\n");
        cJSON* item = NULL;
        int count = 0;
        cJSON_ArrayForEach(item, items) {
            if (cJSON_IsNumber(item) && count < 20) {
                result = sdscatprintf(result, "  - Item #%d\n", item->valueint);
                count++;
            }
        }
    }

    cJSON* profiles = cJSON_Select(json, ".profiles:a");
    if (profiles) {
        result = sdscat(result, "\nRecent profile changes:\n");
        cJSON* profile = NULL;
        int count = 0;
        cJSON_ArrayForEach(profile, profiles) {
            if (cJSON_IsString(profile) && count < 20) {
                result = sdscatprintf(result, "  - %s\n", profile->valuestring);
                count++;
            }
        }
    }

    cJSON_Delete(json);

    mcp_tool_call_result_add_text(r, result);
    sdsfree(result);
    return r;
}

static McpInputSchema tool_get_updates_schema[] = {
    mcp_input_schema_null
};

static McpTool tool_get_updates = {
    .name = "get_updates",
    .description = "Get recent item and profile changes on HackerNews",
    .handler = get_updates_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_get_updates_schema,
    },
};

static McpToolCallResult* get_item_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* id_json = cJSON_Select(params, ".id:n");
    if (!id_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "id parameter is required");
        return r;
    }

    int id = id_json->valueint;
    char path[128];
    snprintf(path, sizeof(path), "item/%d.json", id);

    cJSON* json = hn_get(path);
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch item from HackerNews");
        return r;
    }

    sds result = sdsempty();

    cJSON* id_field = cJSON_Select(json, ".id:n");
    cJSON* type = cJSON_Select(json, ".type:s");
    cJSON* by = cJSON_Select(json, ".by:s");
    cJSON* time = cJSON_Select(json, ".time:n");
    cJSON* score = cJSON_Select(json, ".score:n");
    cJSON* title = cJSON_Select(json, ".title:s");
    cJSON* url = cJSON_Select(json, ".url:s");
    cJSON* text = cJSON_Select(json, ".text:s");
    cJSON* parent = cJSON_Select(json, ".parent:n");
    cJSON* descendants = cJSON_Select(json, ".descendants:n");

    if (id_field)
        result = sdscatprintf(result, "#%d", id_field->valueint);

    if (title) {
        result = sdscatprintf(result, ": %s\n", title->valuestring);
    } else {
        result = sdscat(result, "\n");
    }

    if (type)
        result = sdscatprintf(result, "  Type: %s\n", type->valuestring);
    if (by)
        result = sdscatprintf(result, "  Author: %s\n", by->valuestring);
    if (score)
        result = sdscatprintf(result, "  Score: %d points\n", score->valueint);
    if (url)
        result = sdscatprintf(result, "  URL: %s\n", url->valuestring);
    if (descendants)
        result = sdscatprintf(result, "  Comments: %d\n", descendants->valueint);
    if (parent)
        result = sdscatprintf(result, "  Parent: #%d\n", parent->valueint);
    if (time) {
        time_t timestamp = time->valuedouble;
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", gmtime(&timestamp));
        result = sdscatprintf(result, "  Time: %s UTC\n", time_str);
    }
    if (text) {
        result = sdscat(result, "\n  Text:\n");
        result = sdscatprintf(result, "  %s\n", text->valuestring);
    }

    cJSON_Delete(json);

    mcp_tool_call_result_add_text(r, result);
    sdsfree(result);
    return r;
}

static McpInputSchema tool_get_item_schema[] = {
    { .name = "id",
      .description = "Item ID to fetch",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static McpTool tool_get_item = {
    .name = "get_item",
    .description = "Get a HackerNews item (story, comment, etc.) by ID",
    .handler = get_item_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_get_item_schema,
    },
};

static McpToolCallResult* get_user_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* id_json = cJSON_Select(params, ".id:s");
    if (!id_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "id parameter is required");
        return r;
    }

    const char* id = id_json->valuestring;
    char* id_escaped = curl_easy_escape(NULL, id, 0);
    char path[256];
    snprintf(path, sizeof(path), "user/%s.json", id_escaped);
    curl_free(id_escaped);

    cJSON* json = hn_get(path);
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch user from HackerNews");
        return r;
    }

    sds result = sdsempty();

    cJSON* id_field = cJSON_Select(json, ".id:s");
    cJSON* karma = cJSON_Select(json, ".karma:n");
    cJSON* created = cJSON_Select(json, ".created:n");
    cJSON* about = cJSON_Select(json, ".about:s");
    cJSON* submitted = cJSON_Select(json, ".submitted:a");

    if (id_field)
        result = sdscatprintf(result, "User: %s\n", id_field->valuestring);
    if (karma)
        result = sdscatprintf(result, "  Karma: %d\n", karma->valueint);
    if (created) {
        time_t timestamp = created->valuedouble;
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", gmtime(&timestamp));
        result = sdscatprintf(result, "  Created: %s UTC\n", time_str);
    }
    if (submitted) {
        int submitted_count = cJSON_GetArraySize(submitted);
        result = sdscatprintf(result, "  Submitted: ~%d items\n", submitted_count);
    }
    if (about) {
        result = sdscat(result, "\n  About:\n");
        result = sdscatprintf(result, "  %s\n", about->valuestring);
    }

    cJSON_Delete(json);

    mcp_tool_call_result_add_text(r, result);
    sdsfree(result);
    return r;
}

static McpInputSchema tool_get_user_schema[] = {
    { .name = "id",
      .description = "User ID to fetch",
      .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    mcp_input_schema_null
};

static McpTool tool_get_user = {
    .name = "get_user",
    .description = "Get a HackerNews user profile by ID",
    .handler = get_user_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_get_user_schema,
    },
};

static McpToolCallResult* get_top_stories_handler(cJSON* params)
{
    int limit = 20;
    cJSON* limit_json = cJSON_Select(params, ".limit:n");
    if (limit_json) {
        limit = limit_json->valueint;
        if (limit < 1) limit = 20;
        if (limit > 100) limit = 100;
    }

    return fetch_stories("topstories.json", limit);
}

static McpInputSchema tool_get_stories_schema[] = {
    { .name = "limit",
      .description = "Maximum number of stories to return (optional, default: 20, max: 100)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static McpTool tool_get_top_stories = {
    .name = "get_top_stories",
    .description = "Get top stories from HackerNews",
    .handler = get_top_stories_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_get_stories_schema,
    },
};

int main(int argc, const char* argv[])
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    mcp_set_name("hackernews-mcp");
    mcp_set_version("1.0.0");
    mcp_add_tool(&tool_get_max_item);
    mcp_add_tool(&tool_get_updates);
    mcp_add_tool(&tool_get_item);
    mcp_add_tool(&tool_get_user);
    mcp_add_tool(&tool_get_top_stories);

    fprintf(stderr, "HackerNews MCP Server running...\n");
    mcp_main(argc, argv);

    curl_global_cleanup();
    return 0;
}
