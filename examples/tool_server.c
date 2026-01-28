/**
 * @file tool_server.c
 * @brief MCP server with simple tool implementations
 * 
 * This server provides basic calculator tools (add, multiply).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcp.h"
#include "mcp_json.h"

/* strdup implementation for C99 compatibility */
static char* mcp_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}

/* Handle tools/list request */
static char* handle_tools_list(void) {
    /* Return a list of available tools */
    const char *tools = "{"
        "\"tools\":["
            "{"
                "\"name\":\"add\","
                "\"description\":\"Add two numbers\","
                "\"inputSchema\":{"
                    "\"type\":\"object\","
                    "\"properties\":{"
                        "\"a\":{\"type\":\"number\"},"
                        "\"b\":{\"type\":\"number\"}"
                    "},"
                    "\"required\":[\"a\",\"b\"]"
                "}"
            "},"
            "{"
                "\"name\":\"multiply\","
                "\"description\":\"Multiply two numbers\","
                "\"inputSchema\":{"
                    "\"type\":\"object\","
                    "\"properties\":{"
                        "\"a\":{\"type\":\"number\"},"
                        "\"b\":{\"type\":\"number\"}"
                    "},"
                    "\"required\":[\"a\",\"b\"]"
                "}"
            "}"
        "]"
    "}";
    
    return mcp_strdup(tools);
}

/* Handle tools/call request */
static char* handle_tools_call(const char *params) {
    if (!params) {
        return mcp_strdup("{\"error\":\"No parameters provided\"}");
    }
    
    /* Extract tool name */
    char *name = mcp_json_get_string(params, "name");
    if (!name) {
        return mcp_strdup("{\"error\":\"Tool name not specified\"}");
    }
    
    /* Find arguments object in params */
    const char *args_start = strstr(params, "\"arguments\"");
    char *arguments = NULL;
    if (args_start) {
        const char *colon = strchr(args_start, ':');
        if (colon) {
            colon++;
            while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
            
            if (*colon == '{') {
                const char *end = colon + 1;
                int depth = 1;
                while (*end && depth > 0) {
                    if (*end == '{') depth++;
                    else if (*end == '}') depth--;
                    end++;
                }
                
                size_t len = end - colon;
                arguments = malloc(len + 1);
                if (arguments) {
                    memcpy(arguments, colon, len);
                    arguments[len] = '\0';
                }
            }
        }
    }
    
    char *result = NULL;
    
    if (strcmp(name, "add") == 0) {
        /* Extract a and b from arguments */
        int64_t a = 0, b = 0;
        if (arguments) {
            mcp_json_get_int(arguments, "a", &a);
            mcp_json_get_int(arguments, "b", &b);
        }
        
        int64_t sum = a + b;
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "{\"content\":[{\"type\":\"text\",\"text\":\"Result: %lld\"}]}", 
                 (long long)sum);
        result = mcp_strdup(buffer);
        
    } else if (strcmp(name, "multiply") == 0) {
        /* Extract a and b from arguments */
        int64_t a = 0, b = 0;
        if (arguments) {
            mcp_json_get_int(arguments, "a", &a);
            mcp_json_get_int(arguments, "b", &b);
        }
        
        int64_t product = a * b;
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "{\"content\":[{\"type\":\"text\",\"text\":\"Result: %lld\"}]}", 
                 (long long)product);
        result = mcp_strdup(buffer);
        
    } else {
        result = mcp_strdup("{\"error\":\"Unknown tool\"}");
    }
    
    free(name);
    free(arguments);
    
    return result;
}

/* Request handler */
char* tool_handler(const char *method, const char *params, void *user_data) {
    (void)user_data;
    
    if (!method) return NULL;
    
    if (strcmp(method, "tools/list") == 0) {
        return handle_tools_list();
    } else if (strcmp(method, "tools/call") == 0) {
        return handle_tools_call(params);
    }
    
    return NULL;
}

/* Notification handler */
void notification_handler(const char *method, const char *params, void *user_data) {
    (void)user_data;
    fprintf(stderr, "Notification: %s\n", method);
    if (params) {
        fprintf(stderr, "Params: %s\n", params);
    }
}

int main(void) {
    /* Create MCP server */
    mcp_server_t *server = mcp_server_create("calculator-tools", "1.0.0");
    if (!server) {
        fprintf(stderr, "Failed to create MCP server\n");
        return 1;
    }
    
    /* Set capabilities */
    mcp_capabilities_t caps = {0};
    caps.supports_tools = 1;
    mcp_server_set_capabilities(server, caps);
    
    /* Set handlers */
    mcp_server_set_request_handler(server, tool_handler, NULL);
    mcp_server_set_notification_handler(server, notification_handler, NULL);
    
    fprintf(stderr, "Calculator tool server started. Listening on stdin...\n");
    
    /* Run server event loop */
    int result = mcp_server_run(server);
    
    /* Cleanup */
    mcp_server_destroy(server);
    
    return result;
}
