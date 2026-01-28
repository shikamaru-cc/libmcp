/**
 * @file mcp_server.c
 * @brief MCP server implementation
 */

#include "mcp.h"
#include "mcp_json.h"
#include "mcp_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Internal server structure */
struct mcp_server {
    char *name;
    char *version;
    mcp_capabilities_t capabilities;
    mcp_request_handler_t request_handler;
    mcp_notification_handler_t notification_handler;
    void *user_data;
    int initialized;
};

mcp_server_t* mcp_server_create(const char *name, const char *version) {
    if (!name || !version) return NULL;
    
    mcp_server_t *server = calloc(1, sizeof(mcp_server_t));
    if (!server) return NULL;
    
    server->name = mcp_strdup(name);
    server->version = mcp_strdup(version);
    
    if (!server->name || !server->version) {
        mcp_server_destroy(server);
        return NULL;
    }
    
    /* Default capabilities: none */
    server->capabilities.supports_resources = 0;
    server->capabilities.supports_tools = 0;
    server->capabilities.supports_prompts = 0;
    server->capabilities.supports_logging = 0;
    
    server->initialized = 0;
    
    return server;
}

void mcp_server_destroy(mcp_server_t *server) {
    if (!server) return;
    
    free(server->name);
    free(server->version);
    free(server);
}

void mcp_server_set_capabilities(mcp_server_t *server, mcp_capabilities_t capabilities) {
    if (!server) return;
    server->capabilities = capabilities;
}

void mcp_server_set_request_handler(mcp_server_t *server, mcp_request_handler_t handler, void *user_data) {
    if (!server) return;
    server->request_handler = handler;
    server->user_data = user_data;
}

void mcp_server_set_notification_handler(mcp_server_t *server, mcp_notification_handler_t handler, void *user_data) {
    if (!server) return;
    server->notification_handler = handler;
    server->user_data = user_data;
}

/* Handle initialize request */
static char* handle_initialize(mcp_server_t *server, const char *params) {
    (void)params; /* Unused for now */
    
    if (server->initialized) {
        return NULL; /* Already initialized */
    }
    
    server->initialized = 1;
    
    /* Build capabilities object */
    char *cap_obj = mcp_strdup("{}");
    if (!cap_obj) return NULL;
    
    /* Add capabilities if any are supported */
    if (server->capabilities.supports_tools || 
        server->capabilities.supports_resources ||
        server->capabilities.supports_prompts ||
        server->capabilities.supports_logging) {
        
        /* Build incrementally */
        char *new_cap = NULL;
        free(cap_obj);
        
        /* Start with empty object */
        size_t cap_len = 2; /* "{}" */
        cap_obj = malloc(cap_len + 1);
        if (!cap_obj) return NULL;
        cap_obj[0] = '{';
        cap_obj[1] = '}';
        cap_obj[2] = '\0';
        
        /* Add tools if supported */
        if (server->capabilities.supports_tools) {
            new_cap = mcp_json_object("tools", "{}", NULL);
            if (new_cap) {
                free(cap_obj);
                cap_obj = new_cap;
            }
        }
        
        /* For simplicity, only add tools capability for now */
        /* Full implementation would properly merge all capabilities */
    }
    
    if (!cap_obj) return NULL;
    
    /* Build server info */
    char *name_str = mcp_json_string(server->name);
    char *version_str = mcp_json_string(server->version);
    
    if (!name_str || !version_str) {
        free(cap_obj);
        free(name_str);
        free(version_str);
        return NULL;
    }
    
    char *server_info = mcp_json_object("name", name_str, "version", version_str, NULL);
    free(name_str);
    free(version_str);
    
    if (!server_info) {
        free(cap_obj);
        return NULL;
    }
    
    /* Build protocol version */
    char *protocol_version = mcp_json_string(MCP_PROTOCOL_VERSION);
    if (!protocol_version) {
        free(cap_obj);
        free(server_info);
        return NULL;
    }
    
    /* Build result object */
    char *result = mcp_json_object("protocolVersion", protocol_version,
                                   "serverInfo", server_info,
                                   "capabilities", cap_obj,
                                   NULL);
    
    free(protocol_version);
    free(server_info);
    free(cap_obj);
    
    return result;
}

/* Process a single message */
static void process_message(mcp_server_t *server, const char *line) {
    if (!server || !line) return;
    
    mcp_message_t *msg = mcp_message_parse(line);
    if (!msg) {
        /* Parse error */
        mcp_id_t id = {.number = 0};
        mcp_message_t *error = mcp_create_error_response(id, 0, 
                                                         MCP_ERROR_PARSE_ERROR,
                                                         "Failed to parse JSON-RPC message");
        if (error) {
            char *response = mcp_message_serialize(error);
            if (response) {
                printf("%s\n", response);
                fflush(stdout);
                free(response);
            }
            mcp_message_free(error);
        }
        return;
    }
    
    if (msg->type == MCP_MESSAGE_TYPE_REQUEST) {
        /* Handle request */
        char *result = NULL;
        
        /* Check for initialize */
        if (msg->method && strcmp(msg->method, "initialize") == 0) {
            result = handle_initialize(server, msg->params);
        } else if (server->request_handler) {
            result = server->request_handler(msg->method, msg->params, server->user_data);
        }
        
        mcp_message_t *response;
        if (result) {
            response = mcp_create_response(msg->id, msg->id_is_string, result);
            free(result);
        } else {
            response = mcp_create_error_response(msg->id, msg->id_is_string,
                                                 MCP_ERROR_METHOD_NOT_FOUND,
                                                 "Method not found or failed");
        }
        
        if (response) {
            char *response_str = mcp_message_serialize(response);
            if (response_str) {
                printf("%s\n", response_str);
                fflush(stdout);
                free(response_str);
            }
            mcp_message_free(response);
        }
    } else if (msg->type == MCP_MESSAGE_TYPE_NOTIFICATION) {
        /* Handle notification */
        if (server->notification_handler) {
            server->notification_handler(msg->method, msg->params, server->user_data);
        }
    }
    
    mcp_message_free(msg);
}

int mcp_server_run(mcp_server_t *server) {
    if (!server) return -1;
    
    char *line = NULL;
    size_t len = 0;
    ssize_t read_len;
    
    /* Read lines from stdin */
    while ((read_len = mcp_getline(&line, &len, stdin)) != -1) {
        /* Remove trailing newline */
        if (read_len > 0 && line[read_len - 1] == '\n') {
            line[read_len - 1] = '\0';
            read_len--;
        }
        
        /* Skip empty lines */
        if (read_len == 0 || strlen(line) == 0) continue;
        
        /* Process message */
        process_message(server, line);
    }
    
    free(line);
    return 0;
}

int mcp_server_send_notification(mcp_server_t *server, const char *method, const char *params) {
    if (!server || !method) return -1;
    
    mcp_message_t *msg = calloc(1, sizeof(mcp_message_t));
    if (!msg) return -1;
    
    msg->jsonrpc = mcp_strdup(MCP_JSONRPC_VERSION);
    msg->type = MCP_MESSAGE_TYPE_NOTIFICATION;
    msg->method = mcp_strdup(method);
    
    if (params) {
        msg->params = mcp_strdup(params);
    }
    
    char *json = mcp_message_serialize(msg);
    mcp_message_free(msg);
    
    if (!json) return -1;
    
    printf("%s\n", json);
    fflush(stdout);
    free(json);
    
    return 0;
}
