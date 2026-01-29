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
    void(params);

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

    fprintf(stderr, "Redmine MCP Server running...\n");
    fprintf(stderr, "Set REDMINE_URL and REDMINE_API_KEY environment variables\n");

    int result = mcp_server_serve_stdio(server);

    mcp_server_destroy(server);
    curl_global_cleanup();
    return result;
}
