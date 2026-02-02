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

typedef struct {
    int id;
    char* name;
} IssueStatus;

typedef struct {
    int id;
    char* name;
    char* identifier;
    char* description;
} Project;

typedef struct {
    int id;
    char* name;
    int project_id;
} Version;

static const char* redmine_base_url;
static const char* redmine_api_key;
static int redmine_user_id;
static IssueStatus* redmine_issue_statuses = NULL;
static Project* redmine_projects = NULL;
static Version* redmine_versions = NULL;

static cJSON* redmine_get(const char* path)
{
    CURL* curl = NULL;
    struct curl_slist* headers = NULL;
    char* response = NULL;

    while (path && *path == '/') path++;
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", redmine_base_url, path);

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "X-Redmine-API-Key: %s", redmine_api_key);

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

static void redmine_issue_statuses_init()
{
    cJSON* json = redmine_get("issue_statuses.json");
    if (!json)
        return;

    cJSON* statuses = cJSON_Select(json, ".issue_statuses:a");
    if (!statuses) {
        cJSON_Delete(json);
        return;
    }

    cJSON* status = NULL;
    cJSON_ArrayForEach(status, statuses) {
        cJSON* id = cJSON_Select(status, ".id:n");
        cJSON* name = cJSON_Select(status, ".name:s");
        if (id && name) {
            IssueStatus i;
            i.id = id->valueint;
            i.name = strdup(name->valuestring);
            *stb_arr_add(redmine_issue_statuses) = i;
        }
    }

    cJSON_Delete(json);
}

static void redmine_projects_init()
{
    cJSON* json = redmine_get("projects.json");
    if (!json)
        return;

    cJSON* projects = cJSON_Select(json, ".projects:a");
    if (!projects) {
        cJSON_Delete(json);
        return;
    }

    cJSON* project = NULL;
    cJSON_ArrayForEach(project, projects) {
        cJSON* id = cJSON_Select(project, ".id:n");
        cJSON* name = cJSON_Select(project, ".name:s");
        cJSON* identifier = cJSON_Select(project, ".identifier:s");
        cJSON* description = cJSON_Select(project, ".description:s");
        if (id && name && identifier) {
            Project p;
            p.id = id->valueint;
            p.name = strdup(name->valuestring);
            p.identifier = strdup(identifier->valuestring);
            p.description = description ? strdup(description->valuestring) : NULL;
            *stb_arr_add(redmine_projects) = p;
        }
    }

    cJSON_Delete(json);
}

static void redmine_versions_init()
{
    for (int i = 0; i < stb_arr_len(redmine_projects); i++) {
        char path[256];
        snprintf(path, sizeof(path), "projects/%d/versions.json", redmine_projects[i].id);

        cJSON* json = redmine_get(path);
        if (!json)
            continue;

        cJSON* versions = cJSON_Select(json, ".versions:a");
        if (!versions) {
            cJSON_Delete(json);
            continue;
        }

        cJSON* version = NULL;
        cJSON_ArrayForEach(version, versions) {
            cJSON* id = cJSON_Select(version, ".id:n");
            cJSON* name = cJSON_Select(version, ".name:s");
            if (id && name) {
                Version v;
                v.id = id->valueint;
                v.name = strdup(name->valuestring);
                v.project_id = redmine_projects[i].id;
                *stb_arr_add(redmine_versions) = v;
            }
        }

        cJSON_Delete(json);
    }
}

static void redmine_user_id_init()
{
    cJSON* json = redmine_get("users/current.json");
    if (!json)
        return;

    cJSON* user_id = cJSON_Select(json, ".user.id:n");
    if (user_id) {
        redmine_user_id = user_id->valueint;
    }

    cJSON_Delete(json);
}

static void redmine_issue_statuses_cleanup()
{
    for (int i = 0; i < stb_arr_len(redmine_issue_statuses); i++)
        free(redmine_issue_statuses[i].name);
    stb_arr_free(redmine_issue_statuses);
}

static void redmine_projects_cleanup()
{
    for (int i = 0; i < stb_arr_len(redmine_projects); i++) {
        free(redmine_projects[i].name);
        free(redmine_projects[i].identifier);
        if (redmine_projects[i].description)
            free(redmine_projects[i].description);
    }
    stb_arr_free(redmine_projects);
}

static void redmine_versions_cleanup()
{
    for (int i = 0; i < stb_arr_len(redmine_versions); i++)
        free(redmine_versions[i].name);
    stb_arr_free(redmine_versions);
}

static const char* redmine_status_id_to_name(int id)
{
    for (int i = 0; i < stb_arr_len(redmine_issue_statuses); i++) {
        if (redmine_issue_statuses[i].id == id)
            return redmine_issue_statuses[i].name;
    }
    return NULL;
}

static const char* redmine_version_id_to_name(int id)
{
    for (int i = 0; i < stb_arr_len(redmine_versions); i++) {
        if (redmine_versions[i].id == id)
            return redmine_versions[i].name;
    }
    return NULL;
}

static void redmine_init()
{
    redmine_base_url = getenv("REDMINE_URL");
    redmine_api_key = getenv("REDMINE_API_KEY");
    redmine_user_id_init();
    redmine_projects_init();
    redmine_versions_init();
    redmine_issue_statuses_init();
}

static void redmine_cleanup()
{
    redmine_versions_cleanup();
    redmine_projects_cleanup();
    redmine_issue_statuses_cleanup();
}

static McpToolCallResult* list_projects_handler(cJSON* params)
{
    (void)params;

    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    if (stb_arr_len(redmine_projects) == 0) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "No projects available");
        return r;
    }

    for (int i = 0; i < stb_arr_len(redmine_projects); i++) {
        Project* p = &redmine_projects[i];
        mcp_tool_call_result_add_textf(r,
            "ID: %d\nName: %s\nIdentifier: %s\nDescription: %s\n",
            p->id,
            p->name ? p->name : "N/A",
            p->identifier ? p->identifier : "N/A",
            p->description ? p->description : "N/A");
    }

    return r;
}

static int activity_compare_by_created_on(const void* a, const void* b)
{
    const cJSON* act_a = *(const cJSON**)a;
    const cJSON* act_b = *(const cJSON**)b;
    cJSON* time_a = cJSON_GetObjectItem(act_a, "created_on");
    cJSON* time_b = cJSON_GetObjectItem(act_b, "created_on");
    return strcmp(time_a->valuestring, time_b->valuestring);
}

static McpToolCallResult* list_activities_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* user_id_param = cJSON_Select(params, ".user_id:n");
    int user_id = user_id_param ? user_id_param->valueint : redmine_user_id;

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
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch issues from Redmine");
        return r;
    }

    cJSON* issues = cJSON_Select(issues_json, ".issues:a");
    if (!issues) {
        cJSON_Delete(issues_json);
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "No issues found in response");
        return r;
    }

    cJSON** activities = NULL;
    cJSON* issue = NULL;
    cJSON_ArrayForEach(issue, issues) {
        cJSON* id = cJSON_Select(issue, ".id:n");
        cJSON* subject = cJSON_Select(issue, ".subject:s");
        if (!id || !subject) continue;

        int issue_id = id->valueint;
        char detail_path[256];
        snprintf(detail_path, sizeof(detail_path), "issues/%d.json?include=journals", issue_id);
        cJSON* detail_json = redmine_get(detail_path);
        if (!detail_json) continue;

        cJSON* journals = cJSON_Select(detail_json, ".issue.journals:a");
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
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "No activities found in the specified period");
        return r;
    }

    qsort(activities, stb_arr_len(activities), sizeof(cJSON*), activity_compare_by_created_on);

    for (int i = 0; i < stb_arr_len(activities); i++) {
        cJSON* created_on = cJSON_Select(activities[i], ".created_on:s");
        char date[11];
        strncpy(date, created_on->valuestring, 10);
        date[10] = '\0';

        int issue_id = cJSON_Select(activities[i], ".issue_id:n")->valueint;
        const char* issue_subject = cJSON_Select(activities[i], ".subject:s")->valuestring;
        char* time_str = &created_on->valuestring[11];
        time_str[8] = '\0';

        cJSON* details = cJSON_Select(activities[i], ".details:a");
        if (details) {
            cJSON* detail = NULL;
            cJSON_ArrayForEach(detail, details) {
                cJSON* name = cJSON_Select(detail, ".name:s");
                cJSON* old_value = cJSON_Select(detail, ".old_value:s");
                cJSON* new_value = cJSON_Select(detail, ".new_value:s");

                if (name && strcmp(name->valuestring, "description") == 0)
                    /* description is too long, not print change info */
                    mcp_tool_call_result_add_textf(r,
                        "%s: %s (%d %s) modified %s\n",
                        date, time_str, issue_id, issue_subject,
                        name->valuestring);
                else if (name && strcmp(name->valuestring, "status_id") == 0)
                    mcp_tool_call_result_add_textf(r,
                        "%s: %s (%d %s) modified %s from %s to %s\n",
                        date, time_str, issue_id, issue_subject,
                        name->valuestring,
                        redmine_status_id_to_name(atoi(old_value->valuestring)),
                        redmine_status_id_to_name(atoi(new_value->valuestring)));
                else if (name && strcmp(name->valuestring, "fixed_version_id") == 0)
                    mcp_tool_call_result_add_textf(r,
                        "%s: %s (%d %s) modified %s from %s to %s\n",
                        date, time_str, issue_id, issue_subject,
                        name->valuestring,
                        redmine_version_id_to_name(atoi(old_value->valuestring)),
                        redmine_version_id_to_name(atoi(new_value->valuestring)));
                else
                    mcp_tool_call_result_add_textf(r,
                        "%s: %s (%d %s) modified %s from %s to %s\n",
                        date, time_str, issue_id, issue_subject,
                        name ? name->valuestring : "",
                        old_value ? old_value->valuestring : "",
                        new_value ? new_value->valuestring : "");
            }
        }

        cJSON* notes = cJSON_Select(activities[i], ".notes:s");
        size_t len = notes ? strlen(notes->valuestring) : 0;
        if (len > 0) {
            char short_notes[128];
            size_t notes_max = 64;
            if (len > notes_max) {
                memcpy(short_notes, notes->valuestring, notes_max);
                strcpy(short_notes + notes_max, "...");
            } else {
                strcpy(short_notes, notes->valuestring);
            }

            mcp_tool_call_result_add_textf(r,
                "%s: %s (%d %s) comment: %s\n",
                date, time_str, issue_id, issue_subject, short_notes);
        }
    }

    for (int i = 0; i < stb_arr_len(activities); i++) {
        cJSON_Delete(activities[i]);
    }
    stb_arr_free(activities);

    return r;
}

static McpToolCallResult* search_wiki_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* query_json = cJSON_GetObjectItem(params, "q");
    if (!query_json || !cJSON_IsString(query_json)) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Query parameter 'q' is required");
        return r;
    }

    const char* query = query_json->valuestring;

    cJSON* project_identifier_json = cJSON_GetObjectItem(params, "project_identifier");
    const char* project_identifier = project_identifier_json && cJSON_IsString(project_identifier_json)
        ? project_identifier_json->valuestring
        : NULL;

    int limit = 25;
    cJSON* limit_json = cJSON_GetObjectItem(params, "limit");
    if (limit_json && cJSON_IsNumber(limit_json)) {
        limit = limit_json->valueint;
        if (limit < 1) limit = 25;
    }

    int offset = 0;
    cJSON* offset_json = cJSON_GetObjectItem(params, "offset");
    if (offset_json && cJSON_IsNumber(offset_json)) {
        offset = offset_json->valueint;
        if (offset < 0) offset = 0;
    }

    int all_words = 0;
    cJSON* all_words_json = cJSON_GetObjectItem(params, "all_words");
    if (all_words_json && cJSON_IsTrue(all_words_json)) {
        all_words = 1;
    }

    int titles_only = 0;
    cJSON* titles_only_json = cJSON_GetObjectItem(params, "titles_only");
    if (titles_only_json && cJSON_IsTrue(titles_only_json)) {
        titles_only = 1;
    }

    char* query_escaped = curl_easy_escape(NULL, query, 0);
    char search_path[512];
    int written = snprintf(search_path, sizeof(search_path),
        "search.json?q=%s&wiki_pages=1&limit=%d&offset=%d&all_words=%d&titles_only=%d",
        query_escaped, limit, offset, all_words, titles_only);

    if (project_identifier) {
        char* project_escaped = curl_easy_escape(NULL, project_identifier, 0);
        snprintf(search_path + written, sizeof(search_path) - written, "&scope=%s", project_escaped);
        curl_free(project_escaped);
    }

    curl_free(query_escaped);

    cJSON* json = redmine_get(search_path);
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to search wiki pages from Redmine");
        return r;
    }

    cJSON* total_count = cJSON_Select(json, ".total_count:n");
    if (total_count)
        mcp_tool_call_result_add_textf(r, "Total: %d\n", total_count->valueint);

    cJSON* off = cJSON_Select(json, ".offset:n");
    if (off)
        mcp_tool_call_result_add_textf(r, "Offset: %d\n", off->valueint);

    cJSON* result = NULL;
    cJSON* results = cJSON_Select(json, ".results:a");
    cJSON_ArrayForEach(result, results) {
        cJSON* title = cJSON_Select(result, ".title:s");
        cJSON* description = cJSON_Select(result, ".description:s");
        if (!title || !description) continue;
        mcp_tool_call_result_add_textf(r,
            "Title: %s\n"
            "Description: %s\n",
            title->valuestring,
            description->valuestring);
    }

    cJSON_Delete(json);
    return r;
}

static McpInputSchema tool_list_activities_schema[] = {
    { .name = "user_id",
      .description = "User ID to get activities for (optional, can be set via REDMINE_USER_ID env)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "start_date",
      .description = "The start date of user's activities to fetch, should be with format %Y-%m-%d. If empty, setup to 2 weeks ago",
      .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    mcp_input_schema_null
};

static McpTool tool_list_activities = {
    .name = "list_activities",
    .description = "List activities (journals) from assigned issues for a user",
    .handler = list_activities_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_list_activities_schema,
    },
};

static McpInputSchema tool_search_wiki_schema[] = {
    { .name = "q",
      .description = "Search query string",
      .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    { .name = "project_identifier",
      .description = "Limit search to specific project (optional, uses project identifier like 'my-project')",
      .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    { .name = "limit",
      .description = "Maximum number of results to return (optional, default: 25)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "offset",
      .description = "Skip this number of results for pagination (optional, default: 0)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "all_words",
      .description = "Match all query words (true) or any word (false) (optional, default: false)",
      .type = MCP_INPUT_SCHEMA_TYPE_BOOL,
    },
    { .name = "titles_only",
      .description = "Search only in page titles, not content (optional, default: false)",
      .type = MCP_INPUT_SCHEMA_TYPE_BOOL,
    },
    mcp_input_schema_null
};

static McpTool tool_search_wiki = {
    .name = "search_wiki",
    .description = "Search wiki pages across Redmine projects",
    .handler = search_wiki_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_search_wiki_schema,
    },
};

static McpInputSchema tool_list_projects_schema[] = {
    mcp_input_schema_null
};

static McpTool tool_list_projects = {
    .name = "list_projects",
    .description = "List all projects from Redmine",
    .handler = list_projects_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_list_projects_schema,
    },
};

int main(int argc, const char* argv[])
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    redmine_init();

    mcp_set_name("redmine-mcp");
    mcp_set_version("1.0.0");
    mcp_add_tool(&tool_list_projects);
    mcp_add_tool(&tool_list_activities);
    mcp_add_tool(&tool_search_wiki);

    fprintf(stderr, "Redmine MCP Server running...\n");
    mcp_main(argc, argv);

    curl_global_cleanup();
    redmine_cleanup();
    return 0;
}
