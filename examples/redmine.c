#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include "libmcp.h"
#include "cJSON.h"

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    sds* s = (sds*)userp;
    *s = sdscatlen(*s, contents, realsize);
    return realsize;
}

typedef struct redmine_context {
    char* base_url;
    char* api_key;
    CURL* curl;
    struct curl_slist* headers;
} redmine_context_t;

static redmine_context_t* redmine_context_create(void) {
    const char* base_url = getenv("REDMINE_URL");
    const char* api_key = getenv("REDMINE_API_KEY");

    if (!base_url) {
        base_url = "http://localhost:3000";
    }

    if (!api_key) {
        return NULL;
    }

    redmine_context_t* ctx = calloc(1, sizeof(redmine_context_t));
    if (!ctx) {
        return NULL;
    }

    ctx->base_url = strdup(base_url);
    ctx->api_key = strdup(api_key);

    if (!ctx->base_url || !ctx->api_key) {
        free(ctx->base_url);
        free(ctx->api_key);
        free(ctx);
        return NULL;
    }

    ctx->curl = curl_easy_init();
    if (!ctx->curl) {
        free(ctx->base_url);
        free(ctx->api_key);
        free(ctx);
        return NULL;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "X-Redmine-API-Key: %s", ctx->api_key);
    ctx->headers = curl_slist_append(NULL, auth_header);

    curl_easy_setopt(ctx->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, ctx->headers);

    return ctx;
}

static void redmine_context_destroy(redmine_context_t* ctx) {
    if (!ctx) {
        return;
    }

    free(ctx->base_url);
    free(ctx->api_key);

    if (ctx->curl) {
        curl_easy_cleanup(ctx->curl);
    }

    if (ctx->headers) {
        curl_slist_free_all(ctx->headers);
    }

    free(ctx);
}

static cJSON* redmine_http_get(redmine_context_t* ctx, const char* path) {
    char url[512];
    if (path[0] == '/') {
        snprintf(url, sizeof(url), "%s%s", ctx->base_url, path);
    } else {
        snprintf(url, sizeof(url), "%s/%s", ctx->base_url, path);
    }

    sds response = sdsempty();
    curl_easy_setopt(ctx->curl, CURLOPT_URL, url);
    curl_easy_setopt(ctx->curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(ctx->curl);

    if (res != CURLE_OK) {
        sdsfree(response);
        return NULL;
    }

    cJSON* json = cJSON_Parse(response);
    sdsfree(response);

    return json;
}

static int list_projects_handler(cJSON* params, mcp_content_array_t* contents) {
    (void)params;

    redmine_context_t* ctx = redmine_context_create();
    if (!ctx) {
        return mcp_content_add_text(contents, sdsnew("Error: Failed to initialize Redmine context (check REDMINE_API_KEY)"));
    }

    cJSON* json = redmine_http_get(ctx, "projects.json");
    if (!json) {
        redmine_context_destroy(ctx);
        return mcp_content_add_text(contents, sdsnew("Error: Failed to fetch projects from Redmine"));
    }

    cJSON* projects = cJSON_Select(json, ".projects");
    if (!projects || !cJSON_IsArray(projects)) {
        cJSON_Delete(json);
        redmine_context_destroy(ctx);
        return mcp_content_add_text(contents, sdsnew("Error: No projects found in response"));
    }

    sds result = sdsempty();
    result = sdscat(result, "Projects:\n");

    cJSON* project = NULL;
    cJSON_ArrayForEach(project, projects) {
        cJSON* id = cJSON_Select(project, ".id:n");
        cJSON* name = cJSON_Select(project, ".name:s");
        cJSON* identifier = cJSON_Select(project, ".identifier:s");
        cJSON* description = cJSON_Select(project, ".description:s");

        result = sdscatprintf(result, "\n  ID: %d\n", id ? id->valueint : 0);
        result = sdscatprintf(result, "  Name: %s\n", name ? name->valuestring : "N/A");
        result = sdscatprintf(result, "  Identifier: %s\n", identifier ? identifier->valuestring : "N/A");
        if (description && strlen(description->valuestring) > 0) {
            result = sdscatprintf(result, "  Description: %s\n", description->valuestring);
        }
    }

    cJSON_Delete(json);
    redmine_context_destroy(ctx);

    int ret = mcp_content_add_text(contents, result);
    if (ret != 0) {
        sdsfree(result);
        return ret;
    }

    return MCP_ERROR_NONE;
}

static int list_activities_handler(cJSON* params, mcp_content_array_t* contents) {
    const char* user_id_str = getenv("REDMINE_USER_ID");

    cJSON* user_id_json = cJSON_GetObjectItem(params, "user_id");
    int user_id = user_id_json && cJSON_IsNumber(user_id_json) ? user_id_json->valueint : -1;
    if (user_id == -1 && user_id_str) {
        user_id = atoi(user_id_str);
    }

    if (user_id == -1) {
        return mcp_content_add_text(contents, sdsnew("Error: user_id parameter or REDMINE_USER_ID environment variable not set"));
    }

    /* prepare start date */

    static const char* const fmt = "%Y-%m-%d";
    struct tm start_date_tm;
    cJSON* start_date_param = cJSON_Select(params, "start_date:s");
    if (!start_date_param || strptime(start_date_param->valuestring, fmt, &start_date_tm) == NULL) {
        /* default to 2 weeks ago */
        time_t now = time(NULL);
        gmtime_r(&now, &start_date_tm);
        start_date_tm.tm_mday -= 14;
        mktime(&start_date_tm);
    }

    /* issue request */

    redmine_context_t* ctx = redmine_context_create();
    if (!ctx) {
        return mcp_content_add_text(contents, sdsnew("Error: Failed to initialize Redmine context (check REDMINE_API_KEY)"));
    }

    char updated_on[128];
    char* start_date = updated_on+2;
    sprintf(updated_on, ">=");
    strftime(start_date, sizeof(updated_on)-2, "%Y-%m-%d", &start_date_tm);
    char* updated_on_escape = curl_easy_escape(ctx->curl, updated_on, 0);

    char issues_path[256];
    snprintf(issues_path, sizeof(issues_path), "issues.json?assigned_to_id=%d&updated_on=%s&status_id=*&sort=updated_on:desc", user_id, updated_on_escape);

    curl_free(updated_on_escape);

    cJSON* issues_json = redmine_http_get(ctx, issues_path);
    if (!issues_json) {
        redmine_context_destroy(ctx);
        return mcp_content_add_text(contents, sdsnew("Error: Failed to fetch issues from Redmine"));
    }

    cJSON* issues = cJSON_Select(issues_json, ".issues:a");
    if (!issues) {
        cJSON_Delete(issues_json);
        redmine_context_destroy(ctx);
        return mcp_content_add_text(contents, sdsnew("Error: No issues found in response"));
    }

    cJSON** activities = NULL;
    int activity_count = 0;
    int activity_capacity = 0;

    cJSON* issue = NULL;
    cJSON_ArrayForEach(issue, issues) {
        cJSON* id = cJSON_Select(issue, ".id:n");
        if (!id) {
            continue;
        }

        cJSON* subject = cJSON_Select(issue, ".subject:s");
        if (!subject) {
            continue;
        }

        int issue_id = id->valueint;

        char detail_path[256];
        snprintf(detail_path, sizeof(detail_path), "issues/%d.json?include=journals", issue_id);

        cJSON* detail_json = redmine_http_get(ctx, detail_path);
        if (!detail_json) {
            continue;
        }

        cJSON* journals = cJSON_Select(detail_json, ".issue.journals:a");

        if (journals) {
            cJSON* journal = NULL;
            cJSON_ArrayForEach(journal, journals) {
                cJSON* journal_user_id = cJSON_Select(journal, ".user.id:n");
                cJSON* created_on = cJSON_Select(journal, ".created_on:s");

                /* journal created by target user and happened after start_date */
                if (journal_user_id && journal_user_id->valueint == user_id && created_on) {
                    char journal_date[11];
                    strncpy(journal_date, created_on->valuestring, 10);
                    journal_date[10] = '\0';
                    if (strcmp(journal_date, start_date) >= 0) {
                        if (activity_count >= activity_capacity) {
                            activity_capacity = activity_capacity == 0 ? 16 : activity_capacity * 2;
                            activities = realloc(activities, sizeof(cJSON*) * activity_capacity);
                        }

                        activities[activity_count] = cJSON_Duplicate(journal, 1);
                        cJSON* subject_copy = cJSON_CreateString(subject ? subject->valuestring : "N/A");
                        cJSON_AddItemToObject(activities[activity_count], "issue_id", cJSON_CreateNumber(issue_id));
                        cJSON_AddItemToObject(activities[activity_count], "subject", subject_copy);
                        activity_count++;
                    }
                }
            }
        }

        cJSON_Delete(detail_json);
    }

    cJSON_Delete(issues_json);
    redmine_context_destroy(ctx);

    if (activity_count == 0) {
        return mcp_content_add_text(contents, sdsnew("No activities found in the specified period"));
    }

    sds result = sdsempty();

    for (int i = 0; i < activity_count - 1; i++) {
        for (int j = i + 1; j < activity_count; j++) {
            cJSON* time_i = cJSON_GetObjectItem(activities[i], "created_on");
            cJSON* time_j = cJSON_GetObjectItem(activities[j], "created_on");
            if (strcmp(time_i->valuestring, time_j->valuestring) < 0) {
                cJSON* temp = activities[i];
                activities[i] = activities[j];
                activities[j] = temp;
            }
        }
    }

    for (int i = 0; i < activity_count; i++) {
        cJSON* created_on = cJSON_Select(activities[i], ".created_on:s");
        char date[11];
        strncpy(date, created_on->valuestring, 10);
        date[10] = '\0';

        int issue_id = cJSON_Select(activities[i], ".issue_id:n")->valueint;
        char* time_str = &created_on->valuestring[11];
        time_str[8] = '\0';

        cJSON* details = cJSON_Select(activities[i], ".details:a");
        if (details) {
            cJSON* detail = NULL;
            cJSON_ArrayForEach(detail, details) {
                cJSON* name = cJSON_Select(detail, ".name:s");
                result = sdscatprintf(result, "%s: %s (%d) modified %s\n", date, time_str, issue_id,
                                     name ? name->valuestring : "unknown");
            }
        }

        cJSON* notes = cJSON_Select(activities[i], ".notes:s");
        if (notes && strlen(notes->valuestring) > 0) {
            sds short_notes = sdsempty();
            if (strlen(notes->valuestring) > 30) {
                short_notes = sdscatlen(short_notes, notes->valuestring, 30);
                short_notes = sdscat(short_notes, "...");
            } else {
                short_notes = sdscat(short_notes, notes->valuestring);
            }
            result = sdscatprintf(result, "%s: %s (%d) comment: %s\n", date, time_str, issue_id, short_notes);
            sdsfree(short_notes);
        }
    }

    for (int i = 0; i < activity_count; i++) {
        cJSON_Delete(activities[i]);
    }
    free(activities);

    int ret = mcp_content_add_text(contents, result);
    if (ret != 0) {
        sdsfree(result);
        return ret;
    }

    return MCP_ERROR_NONE;
}

static mcp_input_schema_t tool_list_activities_schema[] = {
    {
        .name = "user_id",
        .description = "User ID to get activities for (optional, can be set via REDMINE_USER_ID env)",
        .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    {
        .name = "start_date",
        .description = "The start date of user's activities to fetch, should be with format %Y-%m-%d. If empty, setup to 2 weeks ago",
        .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    mcp_input_schema_null
};

static mcp_tool_t tool_list_activities = {
    .name = "list_activities",
    .description = "List activities (journals) from assigned issues for a user",
    .handler = list_activities_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_list_activities_schema,
    },
};

static mcp_input_schema_t tool_list_projects_schema[] = {
    mcp_input_schema_null
};

static mcp_tool_t tool_list_projects = {
    .name = "list_projects",
    .description = "List all projects from Redmine",
    .handler = list_projects_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_list_projects_schema,
    },
};

int main(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    mcp_server_t* server = mcp_server_create();
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        curl_global_cleanup();
        return 1;
    }

    mcp_server_set_name(server, "redmine-mcp");
    mcp_server_set_version(server, "1.0.0");

    mcp_server_register_tool(server, &tool_list_projects);
    mcp_server_register_tool(server, &tool_list_activities);

    fprintf(stderr, "Redmine MCP Server running...\n");
    fprintf(stderr, "Set REDMINE_URL and REDMINE_API_KEY environment variables\n");

    int result = mcp_server_serve_stdio(server);

    mcp_server_destroy(server);
    curl_global_cleanup();
    return result;
}
