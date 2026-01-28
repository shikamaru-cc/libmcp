#include "libmcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "cJSON.h"

#define MCP_MAX_TOOLS 128
#define MCP_MAX_PROMPTS 128
#define MCP_BUFFER_SIZE 8192

typedef struct {
    mcp_tool_t tool;
    mcp_tool_handler_t handler;
    void* user_data;
} mcp_registered_tool_t;

typedef struct {
    mcp_prompt_t prompt;
    mcp_prompt_handler_t handler;
    void* user_data;
} mcp_registered_prompt_t;

struct mcp_server {
    char* name;
    char* version;
    mcp_registered_tool_t tools[MCP_MAX_TOOLS];
    int tool_count;
    mcp_registered_prompt_t prompts[MCP_MAX_PROMPTS];
    int prompt_count;
    int running;
};

struct mcp_connection {
    mcp_server_t* server;
    FILE* in;
    FILE* out;
};

static char* read_jsonrpc_message(int fd) {
    static char buffer[MCP_BUFFER_SIZE];
    char* msg = NULL;
    size_t msglen = 0;
    while (1) {
        ssize_t rc = read(fd, buffer, sizeof(buffer));
        if (rc < 0) {
            free(msg);
            return NULL;
        }

        if (rc == 0) {
            return msg;
        }

        msg = realloc(msg, msglen + rc);
        memcpy(msg + msglen, buffer, rc);
        msglen += rc;

        if (msg[msglen-1] == '\n') {
            msg[msglen-1] = '\0';
            if (msglen > 2 && msg[msglen-2] == '\r') {
                msg[msglen-2] = '\0';
            }
            return msg;
        }
    }
}

static int write_jsonrpc_message(FILE* out, const char* json_str) {
    fwrite(json_str, 1, strlen(json_str), out);
    fwrite("\n", 1, 1, out);
    fflush(out);
    return 0;
}

const char* mcp_error_string(int code) {
    switch (code) {
        case MCP_ERROR_NONE: return "Success";
        case MCP_ERROR_INVALID_ARGUMENT: return "Invalid argument";
        case MCP_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case MCP_ERROR_NOT_FOUND: return "Not found";
        case MCP_ERROR_PROTOCOL: return "Protocol error";
        case MCP_ERROR_IO: return "I/O error";
        case MCP_ERROR_NOT_IMPLEMENTED: return "Not implemented";
        default: return "Unknown error";
    }
}

mcp_server_t* mcp_server_create(void) {
    mcp_server_t* server = calloc(1, sizeof(mcp_server_t));
    if (!server) {
        return NULL;
    }

    server->name = strdup("mcp-server");
    server->version = strdup("1.0.0");

    if (!server->name || !server->version) {
        mcp_server_destroy(server);
        return NULL;
    }

    return server;
}

void mcp_server_destroy(mcp_server_t* server) {
    if (!server) {
        return;
    }

    free(server->name);
    free(server->version);
    free(server);
}

int mcp_server_set_name(mcp_server_t* server, const char* name) {
    if (!server || !name) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }

    char* new_name = strdup(name);
    if (!new_name) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }

    free(server->name);
    server->name = new_name;
    return MCP_ERROR_NONE;
}

int mcp_server_set_version(mcp_server_t* server, const char* version) {
    if (!server || !version) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }

    char* new_version = strdup(version);
    if (!new_version) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }

    free(server->version);
    server->version = new_version;
    return MCP_ERROR_NONE;
}

int mcp_server_register_tool(
    mcp_server_t* server,
    const mcp_tool_t* tool,
    mcp_tool_handler_t handler,
    void* user_data
) {
    if (!server || !tool || !tool->name || !handler) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }

    if (server->tool_count >= MCP_MAX_TOOLS) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }

    server->tools[server->tool_count].tool = *tool;
    server->tools[server->tool_count].handler = handler;
    server->tools[server->tool_count].user_data = user_data;
    server->tool_count++;

    return MCP_ERROR_NONE;
}

int mcp_server_register_prompt(
    mcp_server_t* server,
    const mcp_prompt_t* prompt,
    mcp_prompt_handler_t handler,
    void* user_data
) {
    if (!server || !prompt || !prompt->name || !handler) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }

    if (server->prompt_count >= MCP_MAX_PROMPTS) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }

    server->prompts[server->prompt_count].prompt = *prompt;
    server->prompts[server->prompt_count].handler = handler;
    server->prompts[server->prompt_count].user_data = user_data;
    server->prompt_count++;

    return MCP_ERROR_NONE;
}

static cJSON* handle_initialize(mcp_server_t* server, cJSON* params) {
    (void)params;
    cJSON* response = cJSON_CreateObject();

    /* Protocol version */
    cJSON_AddStringToObject(response, "protocolVersion", "2025-03-26");

    /* Server capabilities */
    cJSON* capabilities = cJSON_CreateObject();

    /* Tools capability - only if tools are registered or will be */
    cJSON* tools_cap = cJSON_CreateObject();
    cJSON_AddBoolToObject(tools_cap, "listChanged", cJSON_False);
    cJSON_AddItemToObject(capabilities, "tools", tools_cap);

    /* Prompts capability - only if prompts are registered or will be */
    cJSON* prompts_cap = cJSON_CreateObject();
    cJSON_AddBoolToObject(prompts_cap, "listChanged", cJSON_False);
    cJSON_AddItemToObject(capabilities, "prompts", prompts_cap);

    cJSON_AddItemToObject(response, "capabilities", capabilities);

    /* Server info */
    cJSON* server_info = cJSON_CreateObject();
    cJSON_AddStringToObject(server_info, "name", server->name);
    cJSON_AddStringToObject(server_info, "version", server->version);
    cJSON_AddItemToObject(response, "serverInfo", server_info);

    return response;
}

static cJSON* handle_tools_list(mcp_server_t* server, cJSON* params) {
    (void)params;
    cJSON* tools = cJSON_CreateArray();

    for (int i = 0; i < server->tool_count; i++) {
        cJSON* tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", server->tools[i].tool.name);
        cJSON_AddStringToObject(tool, "description",
            server->tools[i].tool.description ? server->tools[i].tool.description : "");
        cJSON_AddItemToArray(tools, tool);
    }

    cJSON* response = cJSON_CreateObject();
    cJSON_AddItemToObject(response, "tools", tools);
    return response;
}

static cJSON* handle_tools_call(mcp_server_t* server, cJSON* params) {
    cJSON* name_obj = cJSON_GetObjectItem(params, "name");
    cJSON* args_obj = cJSON_GetObjectItem(params, "arguments");

    if (!name_obj || !cJSON_IsString(name_obj)) {
        return NULL;
    }

    const char* name = name_obj->valuestring;
    char* args_str = args_obj ? cJSON_PrintUnformatted(args_obj) : strdup("{}");

    char* result_str = NULL;
    int error = MCP_ERROR_NOT_FOUND;

    for (int i = 0; i < server->tool_count; i++) {
        if (strcmp(server->tools[i].tool.name, name) == 0) {
            error = server->tools[i].handler(args_str, &result_str,
                server->tools[i].user_data);
            break;
        }
    }

    free(args_str);

    if (error != MCP_ERROR_NONE || !result_str) {
        return NULL;
    }

    cJSON* result = cJSON_Parse(result_str);
    free(result_str);
    return result;
}

static cJSON* handle_prompts_list(mcp_server_t* server, cJSON* params) {
    (void)params;
    cJSON* prompts = cJSON_CreateArray();

    for (int i = 0; i < server->prompt_count; i++) {
        cJSON* prompt = cJSON_CreateObject();
        cJSON_AddStringToObject(prompt, "name", server->prompts[i].prompt.name);
        cJSON_AddStringToObject(prompt, "description",
            server->prompts[i].prompt.description ? server->prompts[i].prompt.description : "");
        cJSON_AddItemToArray(prompts, prompt);
    }

    cJSON* response = cJSON_CreateObject();
    cJSON_AddItemToObject(response, "prompts", prompts);
    return response;
}

static cJSON* handle_prompts_get(mcp_server_t* server, cJSON* params) {
    cJSON* name_obj = cJSON_GetObjectItem(params, "name");
    cJSON* args_obj = cJSON_GetObjectItem(params, "arguments");

    if (!name_obj || !cJSON_IsString(name_obj)) {
        return NULL;
    }

    const char* name = name_obj->valuestring;
    char* args_str = args_obj ? cJSON_PrintUnformatted(args_obj) : strdup("{}");

    char* prompt_text = NULL;
    int error = MCP_ERROR_NOT_FOUND;

    for (int i = 0; i < server->prompt_count; i++) {
        if (strcmp(server->prompts[i].prompt.name, name) == 0) {
            error = server->prompts[i].handler(args_str, &prompt_text,
                server->prompts[i].user_data);
            break;
        }
    }

    free(args_str);

    if (error != MCP_ERROR_NONE || !prompt_text) {
        return NULL;
    }

    cJSON* result = cJSON_CreateObject();
    cJSON* messages = cJSON_CreateArray();
    cJSON* msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", prompt_text);
    cJSON_AddItemToArray(messages, msg);
    cJSON_AddItemToObject(result, "messages", messages);

    free(prompt_text);
    return result;
}

static int process_message(mcp_server_t* server, const char* json_str, char** response) {
    cJSON* request = cJSON_Parse(json_str);
    if (!request) {
        return MCP_ERROR_PROTOCOL;
    }

    cJSON* id = cJSON_GetObjectItem(request, "id");
    cJSON* method = cJSON_GetObjectItem(request, "method");
    cJSON* params = cJSON_GetObjectItem(request, "params");

    if (!method || !cJSON_IsString(method)) {
        cJSON_Delete(request);
        return MCP_ERROR_PROTOCOL;
    }

    cJSON* result = NULL;
    int error = MCP_ERROR_NONE;

    const char* method_name = method->valuestring;

    if (strcmp(method_name, "initialize") == 0) {
        result = handle_initialize(server, params);
    } else if (strcmp(method_name, "tools/list") == 0) {
        result = handle_tools_list(server, params);
    } else if (strcmp(method_name, "tools/call") == 0) {
        result = handle_tools_call(server, params);
    } else if (strcmp(method_name, "prompts/list") == 0) {
        result = handle_prompts_list(server, params);
    } else if (strcmp(method_name, "prompts/get") == 0) {
        result = handle_prompts_get(server, params);
    } else {
        error = MCP_ERROR_NOT_IMPLEMENTED;
    }

    cJSON* response_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(response_obj, "jsonrpc", "2.0");

    if (id) {
        cJSON_AddItemReferenceToObject(response_obj, "id", id);
    }

    if (error == MCP_ERROR_NONE && result) {
        cJSON_AddItemToObject(response_obj, "result", result);
    } else {
        cJSON* error_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(error_obj, "code", error);
        cJSON_AddStringToObject(error_obj, "message", mcp_error_string(error));
        cJSON_AddItemToObject(response_obj, "error", error_obj);
    }

    *response = cJSON_PrintUnformatted(response_obj);
    cJSON_Delete(response_obj);
    cJSON_Delete(request);

    return MCP_ERROR_NONE;
}

int mcp_server_serve_stdio(mcp_server_t* server) {
    if (!server) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }

    server->running = 1;

    while (server->running) {
        char* json_str = read_jsonrpc_message(STDIN_FILENO);
        if (!json_str) {
            break;
        }

        fprintf(stderr, "CLI: %s\n", json_str);

        char* response = NULL;
        int error = process_message(server, json_str, &response);

        fprintf(stderr, "SRV: %s\n", response);

        fprintf(stdout, "%s\n", response);
        fflush(stdout);

        free(json_str);
        free(response);
    }

    return MCP_ERROR_NONE;
}

int mcp_server_serve(mcp_server_t* server, const char* address, int port) {
    (void)server;
    (void)address;
    (void)port;
    return MCP_ERROR_NOT_IMPLEMENTED;
}
