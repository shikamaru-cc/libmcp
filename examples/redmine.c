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

static cJSON* redmine_post(const char* path, const char* data)
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
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (headers == NULL)
        goto fail;

    response = strdup("");
    if (response == NULL)
        goto fail;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

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

static McpToolCallResult* get_issue_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* issue_id_json = cJSON_Select(params, ".issue_id:n");
    if (!issue_id_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "issue_id parameter is required");
        return r;
    }

    int issue_id = issue_id_json->valueint;
    char path[128];
    snprintf(path, sizeof(path), "issues/%d.json", issue_id);

    cJSON* json = redmine_get(path);
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch issue from Redmine");
        return r;
    }

    cJSON* issue = cJSON_Select(json, ".issue");
    if (!issue) {
        cJSON_Delete(json);
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Invalid issue response");
        return r;
    }

    cJSON* id = cJSON_Select(issue, ".id:n");
    cJSON* subject = cJSON_Select(issue, ".subject:s");
    cJSON* description = cJSON_Select(issue, ".description:s");
    cJSON* status = cJSON_Select(issue, ".status.name:s");
    cJSON* priority = cJSON_Select(issue, ".priority.name:s");
    cJSON* assigned_to = cJSON_Select(issue, ".assigned_to.name:s");
    cJSON* author = cJSON_Select(issue, ".author.name:s");
    cJSON* created_on = cJSON_Select(issue, ".created_on:s");
    cJSON* updated_on = cJSON_Select(issue, ".updated_on:s");
    cJSON* project = cJSON_Select(issue, ".project.name:s");
    cJSON* tracker = cJSON_Select(issue, ".tracker.name:s");

    mcp_tool_call_result_add_textf(r, "Issue #%d\n", id ? id->valueint : issue_id);
    if (subject)
        mcp_tool_call_result_add_textf(r, "Subject: %s\n", subject->valuestring);
    if (project)
        mcp_tool_call_result_add_textf(r, "Project: %s\n", project->valuestring);
    if (tracker)
        mcp_tool_call_result_add_textf(r, "Tracker: %s\n", tracker->valuestring);
    if (status)
        mcp_tool_call_result_add_textf(r, "Status: %s\n", status->valuestring);
    if (priority)
        mcp_tool_call_result_add_textf(r, "Priority: %s\n", priority->valuestring);
    if (author)
        mcp_tool_call_result_add_textf(r, "Author: %s\n", author->valuestring);
    if (assigned_to)
        mcp_tool_call_result_add_textf(r, "Assigned to: %s\n", assigned_to->valuestring);
    if (created_on)
        mcp_tool_call_result_add_textf(r, "Created: %s\n", created_on->valuestring);
    if (updated_on)
        mcp_tool_call_result_add_textf(r, "Updated: %s\n", updated_on->valuestring);
    if (description) {
        mcp_tool_call_result_add_text(r, "Description:\n");
        mcp_tool_call_result_add_textf(r, "%s\n", description->valuestring);
    }

    cJSON* journals = cJSON_Select(issue, ".journals:a");
    if (journals && cJSON_GetArraySize(journals) > 0) {
        mcp_tool_call_result_add_text(r, "\nJournal/Notes:\n");
        cJSON* journal = NULL;
        cJSON_ArrayForEach(journal, journals) {
            cJSON* user = cJSON_Select(journal, ".user.name:s");
            cJSON* notes = cJSON_Select(journal, ".notes:s");
            cJSON* created_on_j = cJSON_Select(journal, ".created_on:s");
            
            if (created_on_j)
                mcp_tool_call_result_add_textf(r, "[%s] ", created_on_j->valuestring);
            if (user)
                mcp_tool_call_result_add_textf(r, "%s", user->valuestring);
            if (notes)
                mcp_tool_call_result_add_textf(r, ": %s", notes->valuestring);
            mcp_tool_call_result_add_text(r, "\n");
        }
    }

    cJSON_Delete(json);
    return r;
}

static McpInputSchema tool_get_issue_schema[] = {
    { .name = "issue_id",
      .description = "Issue ID to fetch",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static McpTool tool_get_issue = {
    .name = "get_issue",
    .description = "Get detailed information about a specific issue",
    .handler = get_issue_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_get_issue_schema,
    },
};

static McpToolCallResult* list_issues_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    int limit = 25;
    cJSON* limit_json = cJSON_Select(params, ".limit:n");
    if (limit_json) {
        limit = limit_json->valueint;
        if (limit < 1) limit = 25;
    }

    int offset = 0;
    cJSON* offset_json = cJSON_Select(params, ".offset:n");
    if (offset_json) {
        offset = offset_json->valueint;
        if (offset < 0) offset = 0;
    }

    char query[512] = "";
    size_t query_len = 0;

    cJSON* project_id_json = cJSON_Select(params, ".project_id:n");
    if (project_id_json) {
        query_len += snprintf(query + query_len, sizeof(query) - query_len,
            "project_id=%d&", project_id_json->valueint);
    }

    cJSON* status_id_json = cJSON_Select(params, ".status_id:n");
    if (status_id_json) {
        query_len += snprintf(query + query_len, sizeof(query) - query_len,
            "status_id=%d&", status_id_json->valueint);
    }

    cJSON* assigned_to_id_json = cJSON_Select(params, ".assigned_to_id:n");
    if (assigned_to_id_json) {
        query_len += snprintf(query + query_len, sizeof(query) - query_len,
            "assigned_to_id=%d&", assigned_to_id_json->valueint);
    }

    cJSON* tracker_id_json = cJSON_Select(params, ".tracker_id:n");
    if (tracker_id_json) {
        query_len += snprintf(query + query_len, sizeof(query) - query_len,
            "tracker_id=%d&", tracker_id_json->valueint);
    }

    char path[512];
    snprintf(path, sizeof(path), "issues.json?%slimit=%d&offset=%d&sort=updated_on:desc",
        query, limit, offset);

    cJSON* json = redmine_get(path);
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch issues from Redmine");
        return r;
    }

    cJSON* total_count = cJSON_Select(json, ".total_count:n");
    if (total_count)
        mcp_tool_call_result_add_textf(r, "Total: %d\n", total_count->valueint);

    cJSON* off = cJSON_Select(json, ".offset:n");
    if (off)
        mcp_tool_call_result_add_textf(r, "Offset: %d\n", off->valueint);

    mcp_tool_call_result_add_text(r, "\n");

    cJSON* issues = cJSON_Select(json, ".issues:a");
    if (!issues) {
        cJSON_Delete(json);
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "No issues found");
        return r;
    }

    cJSON* issue = NULL;
    cJSON_ArrayForEach(issue, issues) {
        cJSON* id = cJSON_Select(issue, ".id:n");
        cJSON* subject = cJSON_Select(issue, ".subject:s");
        cJSON* status = cJSON_Select(issue, ".status.name:s");
        cJSON* priority = cJSON_Select(issue, ".priority.name:s");
        cJSON* assigned_to = cJSON_Select(issue, ".assigned_to.name:s");
        cJSON* project = cJSON_Select(issue, ".project.name:s");
        cJSON* updated_on = cJSON_Select(issue, ".updated_on:s");

        mcp_tool_call_result_add_textf(r, "#%d: %s\n", id ? id->valueint : 0, subject ? subject->valuestring : "N/A");
        if (project)
            mcp_tool_call_result_add_textf(r, "  Project: %s\n", project->valuestring);
        if (status)
            mcp_tool_call_result_add_textf(r, "  Status: %s\n", status->valuestring);
        if (priority)
            mcp_tool_call_result_add_textf(r, "  Priority: %s\n", priority->valuestring);
        if (assigned_to)
            mcp_tool_call_result_add_textf(r, "  Assigned to: %s\n", assigned_to->valuestring);
        if (updated_on)
            mcp_tool_call_result_add_textf(r, "  Updated: %s\n", updated_on->valuestring);
        mcp_tool_call_result_add_text(r, "\n");
    }

    cJSON_Delete(json);
    return r;
}

static McpInputSchema tool_list_issues_schema[] = {
    { .name = "project_id",
      .description = "Filter by project ID (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "status_id",
      .description = "Filter by status ID (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "assigned_to_id",
      .description = "Filter by assigned user ID (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "tracker_id",
      .description = "Filter by tracker ID (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "limit",
      .description = "Maximum number of results to return (optional, default: 25)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "offset",
      .description = "Skip this number of results for pagination (optional, default: 0)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static McpTool tool_list_issues = {
    .name = "list_issues",
    .description = "List issues from Redmine with optional filters",
    .handler = list_issues_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_list_issues_schema,
    },
};

static McpToolCallResult* add_issue_note_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* issue_id_json = cJSON_Select(params, ".issue_id:n");
    if (!issue_id_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "issue_id parameter is required");
        return r;
    }

    cJSON* notes_json = cJSON_Select(params, ".notes:s");
    if (!notes_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "notes parameter is required");
        return r;
    }

    int issue_id = issue_id_json->valueint;
    const char* notes = notes_json->valuestring;

    cJSON* issue_obj = cJSON_CreateObject();
    cJSON* notes_obj = cJSON_CreateString(notes);
    cJSON_AddItemToObject(issue_obj, "notes", notes_obj);

    char* data = cJSON_PrintUnformatted(issue_obj);
    cJSON_Delete(issue_obj);

    char path[128];
    snprintf(path, sizeof(path), "issues/%d.json", issue_id);

    cJSON* json = redmine_post(path, data);
    free(data);

    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to add note to issue");
        return r;
    }

    mcp_tool_call_result_add_textf(r, "Note added to issue #%d\n", issue_id);
    cJSON_Delete(json);
    return r;
}

static McpInputSchema tool_add_issue_note_schema[] = {
    { .name = "issue_id",
      .description = "Issue ID to add note to",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "notes",
      .description = "Note text to add",
      .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    mcp_input_schema_null
};

static McpTool tool_add_issue_note = {
    .name = "add_issue_note",
    .description = "Add a note/comment to an existing issue",
    .handler = add_issue_note_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_add_issue_note_schema,
    },
};

static McpToolCallResult* create_issue_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* project_id_json = cJSON_Select(params, ".project_id:n");
    if (!project_id_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "project_id parameter is required");
        return r;
    }

    cJSON* subject_json = cJSON_Select(params, ".subject:s");
    if (!subject_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "subject parameter is required");
        return r;
    }

    cJSON* issue_obj = cJSON_CreateObject();
    cJSON* project_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(project_obj, "id", project_id_json->valueint);
    cJSON_AddItemToObject(issue_obj, "project", project_obj);

    cJSON* subject_str = cJSON_CreateString(subject_json->valuestring);
    cJSON_AddItemToObject(issue_obj, "subject", subject_str);

    cJSON* description_json = cJSON_Select(params, ".description:s");
    if (description_json) {
        cJSON* desc_str = cJSON_CreateString(description_json->valuestring);
        cJSON_AddItemToObject(issue_obj, "description", desc_str);
    }

    cJSON* tracker_id_json = cJSON_Select(params, ".tracker_id:n");
    if (tracker_id_json) {
        cJSON* tracker_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(tracker_obj, "id", tracker_id_json->valueint);
        cJSON_AddItemToObject(issue_obj, "tracker", tracker_obj);
    }

    cJSON* status_id_json = cJSON_Select(params, ".status_id:n");
    if (status_id_json) {
        cJSON* status_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(status_obj, "id", status_id_json->valueint);
        cJSON_AddItemToObject(issue_obj, "status", status_obj);
    }

    cJSON* priority_id_json = cJSON_Select(params, ".priority_id:n");
    if (priority_id_json) {
        cJSON* priority_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(priority_obj, "id", priority_id_json->valueint);
        cJSON_AddItemToObject(issue_obj, "priority", priority_obj);
    }

    cJSON* assigned_to_id_json = cJSON_Select(params, ".assigned_to_id:n");
    if (assigned_to_id_json) {
        cJSON* assigned_to_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(assigned_to_obj, "id", assigned_to_id_json->valueint);
        cJSON_AddItemToObject(issue_obj, "assigned_to", assigned_to_obj);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "issue", issue_obj);

    char* data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    cJSON* json = redmine_post("issues.json", data);
    free(data);

    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to create issue");
        return r;
    }

    cJSON* issue = cJSON_Select(json, ".issue");
    if (issue) {
        cJSON* id = cJSON_Select(issue, ".id:n");
        if (id)
            mcp_tool_call_result_add_textf(r, "Issue #%d created successfully\n", id->valueint);
    }

    cJSON_Delete(json);
    return r;
}

static McpInputSchema tool_create_issue_schema[] = {
    { .name = "project_id",
      .description = "Project ID to create issue in",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "subject",
      .description = "Issue subject/title",
      .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    { .name = "description",
      .description = "Issue description (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    { .name = "tracker_id",
      .description = "Tracker ID (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "status_id",
      .description = "Status ID (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "priority_id",
      .description = "Priority ID (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "assigned_to_id",
      .description = "User ID to assign to (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static McpTool tool_create_issue = {
    .name = "create_issue",
    .description = "Create a new issue in Redmine",
    .handler = create_issue_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_create_issue_schema,
    },
};

static McpToolCallResult* get_wiki_page_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* project_json = cJSON_Select(params, ".project_identifier:s");
    if (!project_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "project_identifier parameter is required");
        return r;
    }

    cJSON* title_json = cJSON_Select(params, ".title:s");
    if (!title_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "title parameter is required");
        return r;
    }

    const char* project_identifier = project_json->valuestring;
    const char* title = title_json->valuestring;

    char* project_escaped = curl_easy_escape(NULL, project_identifier, 0);
    char* title_escaped = curl_easy_escape(NULL, title, 0);

    char path[512];
    snprintf(path, sizeof(path), "projects/%s/wiki/%s.json", project_escaped, title_escaped);

    curl_free(project_escaped);
    curl_free(title_escaped);

    cJSON* json = redmine_get(path);
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch wiki page from Redmine");
        return r;
    }

    cJSON* wiki_page = cJSON_Select(json, ".wiki_page");
    if (!wiki_page) {
        cJSON_Delete(json);
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Wiki page not found");
        return r;
    }

    cJSON* title_r = cJSON_Select(wiki_page, ".title:s");
    cJSON* text = cJSON_Select(wiki_page, ".text:s");
    cJSON* author = cJSON_Select(wiki_page, ".author.name:s");
    cJSON* created_on = cJSON_Select(wiki_page, ".created_on:s");
    cJSON* updated_on = cJSON_Select(wiki_page, ".updated_on:s");
    cJSON* version = cJSON_Select(wiki_page, ".version:n");

    if (title_r)
        mcp_tool_call_result_add_textf(r, "Title: %s\n", title_r->valuestring);
    if (author)
        mcp_tool_call_result_add_textf(r, "Author: %s\n", author->valuestring);
    if (version)
        mcp_tool_call_result_add_textf(r, "Version: %d\n", version->valueint);
    if (created_on)
        mcp_tool_call_result_add_textf(r, "Created: %s\n", created_on->valuestring);
    if (updated_on)
        mcp_tool_call_result_add_textf(r, "Updated: %s\n", updated_on->valuestring);
    if (text) {
        mcp_tool_call_result_add_text(r, "\nContent:\n");
        mcp_tool_call_result_add_textf(r, "%s\n", text->valuestring);
    }

    cJSON_Delete(json);
    return r;
}

static McpInputSchema tool_get_wiki_page_schema[] = {
    { .name = "project_identifier",
      .description = "Project identifier (e.g., 'my-project')",
      .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    { .name = "title",
      .description = "Wiki page title",
      .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    mcp_input_schema_null
};

static McpTool tool_get_wiki_page = {
    .name = "get_wiki_page",
    .description = "Get a specific wiki page content from a project",
    .handler = get_wiki_page_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_get_wiki_page_schema,
    },
};

static McpToolCallResult* list_wiki_pages_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* project_json = cJSON_Select(params, ".project_identifier:s");
    if (!project_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "project_identifier parameter is required");
        return r;
    }

    const char* project_identifier = project_json->valuestring;
    char* project_escaped = curl_easy_escape(NULL, project_identifier, 0);

    char path[512];
    snprintf(path, sizeof(path), "projects/%s/wiki/index.json", project_escaped);
    curl_free(project_escaped);

    cJSON* json = redmine_get(path);
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch wiki pages from Redmine");
        return r;
    }

    cJSON* wiki_pages = cJSON_Select(json, ".wiki_pages:a");
    if (!wiki_pages) {
        cJSON_Delete(json);
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "No wiki pages found");
        return r;
    }

    cJSON* page = NULL;
    cJSON_ArrayForEach(page, wiki_pages) {
        cJSON* title = cJSON_Select(page, ".title:s");
        cJSON* version = cJSON_Select(page, ".version:n");
        cJSON* created_on = cJSON_Select(page, ".created_on:s");
        cJSON* updated_on = cJSON_Select(page, ".updated_on:s");

        if (title)
            mcp_tool_call_result_add_textf(r, "Title: %s\n", title->valuestring);
        if (version)
            mcp_tool_call_result_add_textf(r, "  Version: %d\n", version->valueint);
        if (created_on)
            mcp_tool_call_result_add_textf(r, "  Created: %s\n", created_on->valuestring);
        if (updated_on)
            mcp_tool_call_result_add_textf(r, "  Updated: %s\n", updated_on->valuestring);
        mcp_tool_call_result_add_text(r, "\n");
    }

    cJSON_Delete(json);
    return r;
}

static McpInputSchema tool_list_wiki_pages_schema[] = {
    { .name = "project_identifier",
      .description = "Project identifier (e.g., 'my-project')",
      .type = MCP_INPUT_SCHEMA_TYPE_STRING,
    },
    mcp_input_schema_null
};

static McpTool tool_list_wiki_pages = {
    .name = "list_wiki_pages",
    .description = "List all wiki pages in a project",
    .handler = list_wiki_pages_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_list_wiki_pages_schema,
    },
};

static McpToolCallResult* list_time_entries_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    int limit = 25;
    cJSON* limit_json = cJSON_Select(params, ".limit:n");
    if (limit_json) {
        limit = limit_json->valueint;
        if (limit < 1) limit = 25;
    }

    int offset = 0;
    cJSON* offset_json = cJSON_Select(params, ".offset:n");
    if (offset_json) {
        offset = offset_json->valueint;
        if (offset < 0) offset = 0;
    }

    char query[512] = "";
    size_t query_len = 0;

    cJSON* user_id_json = cJSON_Select(params, ".user_id:n");
    if (user_id_json) {
        query_len += snprintf(query + query_len, sizeof(query) - query_len,
            "user_id=%d&", user_id_json->valueint);
    }

    cJSON* project_id_json = cJSON_Select(params, ".project_id:n");
    if (project_id_json) {
        query_len += snprintf(query + query_len, sizeof(query) - query_len,
            "project_id=%d&", project_id_json->valueint);
    }

    cJSON* issue_id_json = cJSON_Select(params, ".issue_id:n");
    if (issue_id_json) {
        query_len += snprintf(query + query_len, sizeof(query) - query_len,
            "issue_id=%d&", issue_id_json->valueint);
    }

    char path[512];
    snprintf(path, sizeof(path), "time_entries.json?%slimit=%d&offset=%d&sort=spent_on:desc",
        query, limit, offset);

    cJSON* json = redmine_get(path);
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch time entries from Redmine");
        return r;
    }

    cJSON* total_count = cJSON_Select(json, ".total_count:n");
    if (total_count)
        mcp_tool_call_result_add_textf(r, "Total: %d\n", total_count->valueint);

    cJSON* off = cJSON_Select(json, ".offset:n");
    if (off)
        mcp_tool_call_result_add_textf(r, "Offset: %d\n", off->valueint);

    mcp_tool_call_result_add_text(r, "\n");

    cJSON* time_entries = cJSON_Select(json, ".time_entries:a");
    if (!time_entries) {
        cJSON_Delete(json);
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "No time entries found");
        return r;
    }

    cJSON* entry = NULL;
    cJSON_ArrayForEach(entry, time_entries) {
        cJSON* id = cJSON_Select(entry, ".id:n");
        cJSON* project = cJSON_Select(entry, ".project.name:s");
        cJSON* issue = cJSON_Select(entry, ".issue.id:n");
        cJSON* user = cJSON_Select(entry, ".user.name:s");
        cJSON* activity = cJSON_Select(entry, ".activity.name:s");
        cJSON* hours = cJSON_Select(entry, ".hours:n");
        cJSON* comments = cJSON_Select(entry, ".comments:s");
        cJSON* spent_on = cJSON_Select(entry, ".spent_on:s");

        if (id)
            mcp_tool_call_result_add_textf(r, "Entry #%d\n", id->valueint);
        if (project)
            mcp_tool_call_result_add_textf(r, "  Project: %s\n", project->valuestring);
        if (issue)
            mcp_tool_call_result_add_textf(r, "  Issue: #%d\n", issue->valueint);
        if (user)
            mcp_tool_call_result_add_textf(r, "  User: %s\n", user->valuestring);
        if (activity)
            mcp_tool_call_result_add_textf(r, "  Activity: %s\n", activity->valuestring);
        if (hours)
            mcp_tool_call_result_add_textf(r, "  Hours: %.2f\n", hours->valuedouble);
        if (comments)
            mcp_tool_call_result_add_textf(r, "  Comments: %s\n", comments->valuestring);
        if (spent_on)
            mcp_tool_call_result_add_textf(r, "  Date: %s\n", spent_on->valuestring);
        mcp_tool_call_result_add_text(r, "\n");
    }

    cJSON_Delete(json);
    return r;
}

static McpInputSchema tool_list_time_entries_schema[] = {
    { .name = "user_id",
      .description = "Filter by user ID (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "project_id",
      .description = "Filter by project ID (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "issue_id",
      .description = "Filter by issue ID (optional)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "limit",
      .description = "Maximum number of results to return (optional, default: 25)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "offset",
      .description = "Skip this number of results for pagination (optional, default: 0)",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static McpTool tool_list_time_entries = {
    .name = "list_time_entries",
    .description = "List time entries with optional filters",
    .handler = list_time_entries_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_list_time_entries_schema,
    },
};

static McpToolCallResult* get_project_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    if (!r)
        return NULL;

    cJSON* project_id_json = cJSON_Select(params, ".project_id:n");
    if (!project_id_json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "project_id parameter is required");
        return r;
    }

    int project_id = project_id_json->valueint;
    char path[128];
    snprintf(path, sizeof(path), "projects/%d.json?include=trackers,issue_categories", project_id);

    cJSON* json = redmine_get(path);
    if (!json) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Failed to fetch project from Redmine");
        return r;
    }

    cJSON* project = cJSON_Select(json, ".project");
    if (!project) {
        cJSON_Delete(json);
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "Invalid project response");
        return r;
    }

    cJSON* id = cJSON_Select(project, ".id:n");
    cJSON* name = cJSON_Select(project, ".name:s");
    cJSON* identifier = cJSON_Select(project, ".identifier:s");
    cJSON* description = cJSON_Select(project, ".description:s");
    cJSON* created_on = cJSON_Select(project, ".created_on:s");
    cJSON* updated_on = cJSON_Select(project, ".updated_on:s");
    cJSON* status = cJSON_Select(project, ".status:n");

    if (id)
        mcp_tool_call_result_add_textf(r, "ID: %d\n", id->valueint);
    if (name)
        mcp_tool_call_result_add_textf(r, "Name: %s\n", name->valuestring);
    if (identifier)
        mcp_tool_call_result_add_textf(r, "Identifier: %s\n", identifier->valuestring);
    if (description)
        mcp_tool_call_result_add_textf(r, "Description: %s\n", description->valuestring);
    if (status)
        mcp_tool_call_result_add_textf(r, "Status: %d\n", status->valueint);
    if (created_on)
        mcp_tool_call_result_add_textf(r, "Created: %s\n", created_on->valuestring);
    if (updated_on)
        mcp_tool_call_result_add_textf(r, "Updated: %s\n", updated_on->valuestring);

    cJSON* trackers = cJSON_Select(project, ".trackers:a");
    if (trackers && cJSON_GetArraySize(trackers) > 0) {
        mcp_tool_call_result_add_text(r, "\nTrackers:\n");
        cJSON* tracker = NULL;
        cJSON_ArrayForEach(tracker, trackers) {
            cJSON* tracker_id = cJSON_Select(tracker, ".id:n");
            cJSON* tracker_name = cJSON_Select(tracker, ".name:s");
            if (tracker_id && tracker_name)
                mcp_tool_call_result_add_textf(r, "  - #%d: %s\n", tracker_id->valueint, tracker_name->valuestring);
        }
    }

    cJSON* categories = cJSON_Select(project, ".issue_categories:a");
    if (categories && cJSON_GetArraySize(categories) > 0) {
        mcp_tool_call_result_add_text(r, "\nIssue Categories:\n");
        cJSON* category = NULL;
        cJSON_ArrayForEach(category, categories) {
            cJSON* cat_id = cJSON_Select(category, ".id:n");
            cJSON* cat_name = cJSON_Select(category, ".name:s");
            if (cat_id && cat_name)
                mcp_tool_call_result_add_textf(r, "  - #%d: %s\n", cat_id->valueint, cat_name->valuestring);
        }
    }

    cJSON_Delete(json);
    return r;
}

static McpInputSchema tool_get_project_schema[] = {
    { .name = "project_id",
      .description = "Project ID to fetch details for",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static McpTool tool_get_project = {
    .name = "get_project",
    .description = "Get detailed information about a specific project",
    .handler = get_project_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_get_project_schema,
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
