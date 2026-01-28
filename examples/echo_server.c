/**
 * @file echo_server.c
 * @brief Simple MCP echo server example
 * 
 * This server echoes back any request it receives.
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

/* Request handler that echoes back the method and params */
char* echo_handler(const char *method, const char *params, void *user_data) {
    (void)user_data;
    
    if (!method) {
        return mcp_strdup("{\"error\":\"No method provided\"}");
    }
    
    /* Create echo response */
    char *method_str = mcp_json_string(method);
    if (!method_str) return NULL;
    
    char *result;
    if (params) {
        result = mcp_json_object("echo", method_str, "params", params, NULL);
    } else {
        result = mcp_json_object("echo", method_str, NULL);
    }
    
    free(method_str);
    return result;
}

/* Notification handler */
void notification_handler(const char *method, const char *params, void *user_data) {
    (void)user_data;
    fprintf(stderr, "Received notification: %s\n", method);
    if (params) {
        fprintf(stderr, "Params: %s\n", params);
    }
}

int main(void) {
    /* Create MCP server */
    mcp_server_t *server = mcp_server_create("echo-server", "1.0.0");
    if (!server) {
        fprintf(stderr, "Failed to create MCP server\n");
        return 1;
    }
    
    /* Set capabilities */
    mcp_capabilities_t caps = {0};
    caps.supports_tools = 1;
    mcp_server_set_capabilities(server, caps);
    
    /* Set handlers */
    mcp_server_set_request_handler(server, echo_handler, NULL);
    mcp_server_set_notification_handler(server, notification_handler, NULL);
    
    fprintf(stderr, "Echo server started. Listening on stdin...\n");
    
    /* Run server event loop */
    int result = mcp_server_run(server);
    
    /* Cleanup */
    mcp_server_destroy(server);
    
    return result;
}
