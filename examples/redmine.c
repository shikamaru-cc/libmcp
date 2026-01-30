#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include "libmcp.h"
#include "cJSON.h"
#include "stb.h"

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

static struct {
    char* base_url;
    char* api_key;
    int user_id;
} redmine_config;

static cJSON* redmine_get(const char* path)
{
    CURL* curl = NULL;
    struct curl_slist* headers = NULL;
    char* response = NULL;

    while (path && *path == '/') path++;
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", redmine_config.base_url, path);

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "X-Redmine-API-Key: %s", redmine_config.api_key);

    curl = curl_easy_init();
    if (curl == NULL)
        goto fail;

    headers = curl_slist_append(NULL, auth_header);
    if (headers == NULL)
        goto fail;

    response = strdup("");
    if (response == NULL)
        goto fail;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        goto fail;

    cJSON* json = cJSON_Parse(response);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(response);
    return json;

fail:
    if (curl) curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);
    if (response) free(response);
    return NULL;
}

static int list_projects_handler(cJSON* params, McpContentArray* contents)
{
    (void)params;

    cJSON* json = redmine_get("projects.json");
    if (!json) {
        return mcp_content_add_text(contents, "Error: Failed to fetch projects from Redmine");
    }

    cJSON* projects = cJSON_Select(json, ".projects");
    if (!projects || !cJSON_IsArray(projects)) {
        cJSON_Delete(json);
        return mcp_content_add_text(contents, "Error: No projects found in response");
    }

    mcp_content_add_text(contents, "Projects:\n");

    cJSON* project = NULL;
    cJSON_ArrayForEach(project, projects) {
        cJSON* id = cJSON_Select(project, ".id:n");
        cJSON* name = cJSON_Select(project, ".name:s");
        cJSON* identifier = cJSON_Select(project, ".identifier:s");
        cJSON* description = cJSON_Select(project, ".description:s");

        mcp_content_add_textf(contents, "\n  ID: %d\n", id ? id->valueint : 0);
        mcp_content_add_textf(contents, "  Name: %s\n", name ? name->valuestring : "N/A");
        mcp_content_add_textf(contents, "  Identifier: %s\n", identifier ? identifier->valuestring : "N/A");
        if (description && strlen(description->valuestring) > 0) {
            mcp_content_add_textf(contents, "  Description: %s\n", description->valuestring);
        }
    }

    cJSON_Delete(json);

    return MCP_ERROR_NONE;
}

static int activity_compare_by_created_on(const void* a, const void* b)
{
    const cJSON* act_a = *(const cJSON**)a;
    const cJSON* act_b = *(const cJSON**)b;
    cJSON* time_a = cJSON_GetObjectItem(act_a, "created_on");
    cJSON* time_b = cJSON_GetObjectItem(act_b, "created_on");
    return strcmp(time_a->valuestring, time_b->valuestring);
}

static int list_activities_handler(cJSON* params, McpContentArray* contents)
{
    const char* user_id_str = getenv("REDMINE_USER_ID");

    cJSON* user_id_json = cJSON_GetObjectItem(params, "user_id");
    int user_id = user_id_json && cJSON_IsNumber(user_id_json) ? user_id_json->valueint : -1;
    if (user_id == -1 && user_id_str) {
        user_id = atoi(user_id_str);
    }

    if (user_id == -1) {
        return mcp_content_add_text(contents, "Error: user_id parameter or REDMINE_USER_ID environment variable not set");
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

    char updated_on[128];
    char* start_date = updated_on+2;
    sprintf(updated_on, ">=");
    strftime(start_date, sizeof(updated_on)-2, "%Y-%m-%d", &start_date_tm);
    char* updated_on_escape = curl_easy_escape(NULL, updated_on, 0);

    char issues_path[256];
    snprintf(issues_path, sizeof(issues_path), "issues.json?assigned_to_id=%d&updated_on=%s&status_id=*&sort=updated_on:desc", user_id, updated_on_escape);

    curl_free(updated_on_escape);

    cJSON* issues_json = redmine_get(issues_path);
    if (!issues_json) {
        return mcp_content_add_text(contents, "Error: Failed to fetch issues from Redmine");
    }

    cJSON* issues = cJSON_Select(issues_json, ".issues:a");
    if (!issues) {
        cJSON_Delete(issues_json);
        return mcp_content_add_text(contents, "Error: No issues found in response");
    }

    cJSON** activities = NULL;

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

        cJSON* detail_json = redmine_get(detail_path);
        if (!detail_json) {
            continue;
        }

        cJSON* journal = NULL;
        cJSON* journals = cJSON_Select(detail_json, ".issue.journals:a");
        cJSON_ArrayForEach(journal, journals) {
            cJSON* journal_user_id = cJSON_Select(journal, ".user.id:n");
            cJSON* created_on = cJSON_Select(journal, ".created_on:s");

            /* journal created by target user and happened after start_date */
            if (journal_user_id && journal_user_id->valueint == user_id && created_on) {
                char journal_date[11];
                strncpy(journal_date, created_on->valuestring, 10);
                journal_date[10] = '\0';
                if (strcmp(journal_date, start_date) >= 0) {
                    cJSON* new_activity = cJSON_Duplicate(journal, 1);
                    cJSON* subject_copy = cJSON_CreateString(subject ? subject->valuestring : "N/A");
                    cJSON_AddItemToObject(new_activity, "issue_id", cJSON_CreateNumber(issue_id));
                    cJSON_AddItemToObject(new_activity, "subject", subject_copy);
                    *stb_arr_add(activities) = new_activity;
                }
            }
        }

        cJSON_Delete(detail_json);
    }

    cJSON_Delete(issues_json);

    if (stb_arr_len(activities) == 0) {
        return mcp_content_add_text(contents, "No activities found in the specified period");
    }

    mcp_content_add_text(contents, "Activities:\n");

    qsort(activities, stb_arr_len(activities), sizeof(cJSON*), activity_compare_by_created_on);

    for (int i = 0; i < stb_arr_len(activities); i++) {
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
                mcp_content_add_textf(contents, "%s: %s (%d) modified %s\n", date, time_str, issue_id,
                                     name ? name->valuestring : "unknown");
            }
        }

        cJSON* notes = cJSON_Select(activities[i], ".notes:s");
        if (notes && strlen(notes->valuestring) > 0) {
            if (strlen(notes->valuestring) > 30) {
                char short_notes[34];
                memcpy(short_notes, notes->valuestring, 30);
                short_notes[30] = '\0';
                strcpy(short_notes + 30, "...");
                mcp_content_add_textf(contents, "%s: %s (%d) comment: %s\n", date, time_str, issue_id, short_notes);
            } else {
                mcp_content_add_textf(contents, "%s: %s (%d) comment: %s\n", date, time_str, issue_id, notes->valuestring);
            }
        }
    }

    for (int i = 0; i < stb_arr_len(activities); i++) {
        cJSON_Delete(activities[i]);
    }
    stb_arr_free(activities);

    return MCP_ERROR_NONE;
}

static McpInputSchema tool_list_activities_schema[] = {
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

static McpInputSchema tool_list_projects_schema[] = {
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

int main(void)
{
    redmine_config.base_url = getenv("REDMINE_URL");
    redmine_config.api_key = getenv("REDMINE_API_KEY");

    curl_global_init(CURL_GLOBAL_DEFAULT);

    McpServer* server = mcp_server_create();
    mcp_server_set_name(server, "redmine-mcp");
    mcp_server_set_version(server, "1.0.0");

    mcp_server_register_tool(server, &tool_list_projects);
    mcp_server_register_tool(server, &tool_list_activities);

    fprintf(stderr, "Redmine MCP Server running...\n");

    int result = mcp_server_serve_stdio(server);

    mcp_server_destroy(server);
    curl_global_cleanup();
    return result;
}
