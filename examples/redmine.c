#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "libmcp.h"
#include "cJSON.h"

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    sds* s = (sds*)userp;
    *s = sdscatlen(*s, contents, realsize);
    return realsize;
}

static int list_projects_handler(cJSON* params, mcp_content_array_t* contents) {
    (void)params;

    CURL* curl;
    CURLcode res;
    sds response = sdsempty();

    const char* base_url = getenv("REDMINE_URL");
    const char* api_key = getenv("REDMINE_API_KEY");

    if (!base_url) {
        base_url = "http://localhost:3000";
    }

    if (!api_key) {
        return mcp_content_add_text(contents, sdsnew("Error: REDMINE_API_KEY environment variable not set"));
    }

    curl = curl_easy_init();
    if (!curl) {
        sdsfree(response);
        return mcp_content_add_text(contents, sdsnew("Error: Failed to initialize curl"));
    }

    char url[512];
    snprintf(url, sizeof(url), "%s/projects.json", base_url);

    struct curl_slist* headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "X-Redmine-API-Key: %s", api_key);
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        sds error_msg = sdscatprintf(sdsempty(), "Error: curl_easy_perform() failed: %s", curl_easy_strerror(res));
        mcp_content_add_text(contents, error_msg);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        sdsfree(response);
        return MCP_ERROR_IO;
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    cJSON* json = cJSON_Parse(response);
    sdsfree(response);

    if (!json) {
        return mcp_content_add_text(contents, sdsnew("Error: Failed to parse JSON response"));
    }

    cJSON* projects = cJSON_GetObjectItem(json, "projects");
    if (!projects || !cJSON_IsArray(projects)) {
        cJSON_Delete(json);
        return mcp_content_add_text(contents, sdsnew("Error: No projects found in response"));
    }

    sds result = sdsempty();
    result = sdscat(result, "Projects:\n");

    cJSON* project = NULL;
    cJSON_ArrayForEach(project, projects) {
        cJSON* id = cJSON_GetObjectItem(project, "id");
        cJSON* name = cJSON_GetObjectItem(project, "name");
        cJSON* identifier = cJSON_GetObjectItem(project, "identifier");
        cJSON* description = cJSON_GetObjectItem(project, "description");

        result = sdscatprintf(result, "\n  ID: %d\n", cJSON_IsNumber(id) ? id->valueint : 0);
        result = sdscatprintf(result, "  Name: %s\n", cJSON_IsString(name) ? name->valuestring : "N/A");
        result = sdscatprintf(result, "  Identifier: %s\n", cJSON_IsString(identifier) ? identifier->valuestring : "N/A");
        if (description && cJSON_IsString(description) && strlen(description->valuestring) > 0) {
            result = sdscatprintf(result, "  Description: %s\n", description->valuestring);
        }
    }

    cJSON_Delete(json);

    int ret = mcp_content_add_text(contents, result);
    if (ret != 0) {
        sdsfree(result);
        return ret;
    }

    return MCP_ERROR_NONE;
}

static int list_activities_handler(cJSON* params, mcp_content_array_t* contents) {
    const char* base_url = getenv("REDMINE_URL");
    const char* api_key = getenv("REDMINE_API_KEY");
    const char* user_id_str = getenv("REDMINE_USER_ID");
    
    cJSON* user_id_json = cJSON_GetObjectItem(params, "user_id");
    int user_id = user_id_json && cJSON_IsNumber(user_id_json) ? user_id_json->valueint : -1;
    if (user_id == -1 && user_id_str) {
        user_id = atoi(user_id_str);
    }
    
    cJSON* days_json = cJSON_GetObjectItem(params, "days");
    int days = days_json && cJSON_IsNumber(days_json) ? days_json->valueint : 14;
    
    if (!base_url) {
        base_url = "http://localhost:3000";
    }
    
    if (!api_key) {
        return mcp_content_add_text(contents, sdsnew("Error: REDMINE_API_KEY environment variable not set"));
    }
    
    if (user_id == -1) {
        return mcp_content_add_text(contents, sdsnew("Error: user_id parameter or REDMINE_USER_ID environment variable not set"));
    }
    
    char cutoff_date[11];
    time_t now = time(NULL);
    struct tm* tm_now = gmtime(&now);
    tm_now->tm_mday -= days;
    mktime(tm_now);
    strftime(cutoff_date, sizeof(cutoff_date), "%Y-%m-%d", tm_now);
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        return mcp_content_add_text(contents, sdsnew("Error: Failed to initialize curl"));
    }
    
    char url[512];
    snprintf(url, sizeof(url), "%s/issues.json?assigned_to_id=%d&status_id=*&sort=updated_on:desc&limit=100", base_url, user_id);
    
    struct curl_slist* headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "X-Redmine-API-Key: %s", api_key);
    headers = curl_slist_append(headers, auth_header);
    
    sds issues_response = sdsempty();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &issues_response);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        sds error_msg = sdscatprintf(sdsempty(), "Error: curl_easy_perform() failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        sdsfree(issues_response);
        return mcp_content_add_text(contents, error_msg);
    }
    
    curl_slist_free_all(headers);
    
    cJSON* issues_json = cJSON_Parse(issues_response);
    sdsfree(issues_response);
    
    if (!issues_json) {
        curl_easy_cleanup(curl);
        return mcp_content_add_text(contents, sdsnew("Error: Failed to parse issues JSON response"));
    }
    
    cJSON* issues = cJSON_GetObjectItem(issues_json, "issues");
    if (!issues || !cJSON_IsArray(issues)) {
        curl_easy_cleanup(curl);
        cJSON_Delete(issues_json);
        return mcp_content_add_text(contents, sdsnew("Error: No issues found in response"));
    }
    
    cJSON** activities = NULL;
    int activity_count = 0;
    int activity_capacity = 0;
    
    cJSON* issue = NULL;
    cJSON_ArrayForEach(issue, issues) {
        cJSON* id = cJSON_GetObjectItem(issue, "id");
        cJSON* subject = cJSON_GetObjectItem(issue, "subject");
        int issue_id = cJSON_IsNumber(id) ? id->valueint : 0;
        
        snprintf(url, sizeof(url), "%s/issues/%d.json?include=journals", base_url, issue_id);
        
        sds detail_response = sdsempty();
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &detail_response);
        
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            sdsfree(detail_response);
            continue;
        }
        
        cJSON* detail_json = cJSON_Parse(detail_response);
        sdsfree(detail_response);
        
        if (!detail_json) {
            continue;
        }
        
        cJSON* full_issue = cJSON_GetObjectItem(detail_json, "issue");
        cJSON* journals = cJSON_GetObjectItem(full_issue, "journals");
        
        if (journals && cJSON_IsArray(journals)) {
            cJSON* journal = NULL;
            cJSON_ArrayForEach(journal, journals) {
                cJSON* journal_user = cJSON_GetObjectItem(journal, "user");
                cJSON* journal_user_id = cJSON_GetObjectItem(journal_user, "id");
                cJSON* created_on = cJSON_GetObjectItem(journal, "created_on");
                
                if (journal_user_id && cJSON_IsNumber(journal_user_id) && 
                    journal_user_id->valueint == user_id &&
                    created_on && cJSON_IsString(created_on)) {
                    
                    char journal_date[11];
                    strncpy(journal_date, created_on->valuestring, 10);
                    journal_date[10] = '\0';
                    
                    if (strcmp(journal_date, cutoff_date) >= 0) {
                        if (activity_count >= activity_capacity) {
                            activity_capacity = activity_capacity == 0 ? 16 : activity_capacity * 2;
                            activities = realloc(activities, sizeof(cJSON*) * activity_capacity);
                        }
                        
                        activities[activity_count] = cJSON_Duplicate(journal, 1);
                        cJSON* subject_copy = cJSON_CreateString(cJSON_IsString(subject) ? subject->valuestring : "N/A");
                        cJSON_AddItemToObject(activities[activity_count], "issue_id", cJSON_CreateNumber(issue_id));
                        cJSON_AddItemToObject(activities[activity_count], "subject", subject_copy);
                        activity_count++;
                    }
                }
            }
        }
        
        cJSON_Delete(detail_json);
    }
    
    curl_easy_cleanup(curl);
    cJSON_Delete(issues_json);
    
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
        cJSON* created_on = cJSON_GetObjectItem(activities[i], "created_on");
        char date[11];
        strncpy(date, created_on->valuestring, 10);
        date[10] = '\0';
        
        int issue_id = cJSON_GetObjectItem(activities[i], "issue_id")->valueint;
        char* time_str = &created_on->valuestring[11];
        time_str[8] = '\0';
        
        cJSON* details = cJSON_GetObjectItem(activities[i], "details");
        if (details && cJSON_IsArray(details)) {
            cJSON* detail = NULL;
            cJSON_ArrayForEach(detail, details) {
                cJSON* name = cJSON_GetObjectItem(detail, "name");
                result = sdscatprintf(result, "%s: %s (%d) modified %s\n", date, time_str, issue_id, 
                                     cJSON_IsString(name) ? name->valuestring : "unknown");
            }
        }
        
        cJSON* notes = cJSON_GetObjectItem(activities[i], "notes");
        if (notes && cJSON_IsString(notes) && strlen(notes->valuestring) > 0) {
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
        .name = "days",
        .description = "Number of days to look back (default: 14)",
        .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
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
