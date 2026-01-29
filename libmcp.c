#define _POSIX_C_SOURCE 200809L
#include "libmcp.h"
#include "sds.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define MCP_MAX_TOOLS 128
#define MCP_MAX_PROMPTS 128
#define MCP_BUFFER_SIZE 8192

typedef struct {
    mcp_prompt_t prompt;
    mcp_prompt_handler_t handler;
    void* user_data;
} mcp_registered_prompt_t;

struct mcp_server {
    char* name;
    char* version;
    int tool_count;
    mcp_registered_prompt_t prompts[MCP_MAX_PROMPTS];
    int prompt_count;
    int running;

    mcp_tool_t tools[MCP_MAX_TOOLS];
};

struct mcp_connection {
    mcp_server_t* server;
    FILE* in;
    FILE* out;
};

static char* read_jsonrpc_message(FILE* in) {
    if (!in) return NULL;

    char* line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, in);
    if (n == -1) {
        free(line);
        return NULL; /* EOF or error */
    }

    /* Strip trailing LF and optional CR */
    if (n > 0 && line[n-1] == '\n') {
        line[n-1] = '\0';
        if (n > 1 && line[n-2] == '\r') {
            line[n-2] = '\0';
        }
    }

    return line; /* caller must free() */
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

void mcp_server_register_tool(mcp_server_t* server, const mcp_tool_t* tool) {
    if (!server || !tool || !tool->name || !tool->handler) {
        fprintf(stderr, "mcp_server_register_tool: invalid argument\n");
        return;
    }

    if (server->tool_count >= MCP_MAX_TOOLS) {
        fprintf(stderr, "mcp_server_register_tool: max tools reached\n");
        return;
    }

    /* copy tool by value into server registry */
    server->tools[server->tool_count] = *tool;
    server->tool_count++;
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

static const char* schema_type_to_string(mcp_input_schema_type_e t) {
    switch (t) {
        case MCP_INPUT_SCHEMA_TYPE_NUMBER: return "number";
        case MCP_INPUT_SCHEMA_TYPE_STRING: return "string";
        case MCP_INPUT_SCHEMA_TYPE_BOOL:   return "boolean";
        case MCP_INPUT_SCHEMA_TYPE_ARRAY:  return "array";
        case MCP_INPUT_SCHEMA_TYPE_OBJECT: return "object";
        case MCP_INPUT_SCHEMA_TYPE_NULL:   return "null";
        default: return "unknown";
    }
}

/* Convert internal mcp_input_schema_t to a cJSON object representing the schema.
   Returns a new cJSON object or NULL if schema is null/empty. */
static cJSON* mcp_input_schema_to_json(const mcp_input_schema_t* s) {
    if (!s) return NULL;
    if (s->type == MCP_INPUT_SCHEMA_TYPE_NULL) return NULL;

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", schema_type_to_string(s->type));

    if (s->description) {
        cJSON_AddStringToObject(obj, "description", s->description);
    }

    if (s->type == MCP_INPUT_SCHEMA_TYPE_OBJECT) {
        cJSON* props = cJSON_CreateObject();
        const mcp_input_schema_t* p = s->properties;
        while (p && p->type != MCP_INPUT_SCHEMA_TYPE_NULL) {
            cJSON* prop_schema = mcp_input_schema_to_json(p);
            if (p->name && prop_schema) {
                cJSON_AddItemToObject(props, p->name, prop_schema);
            }
            p++;
        }
        cJSON_AddItemToObject(obj, "properties", props);

        cJSON* req = cJSON_CreateArray();
        const char** r = s->required;
        while (r && *r) {
            cJSON_AddItemToArray(req, cJSON_CreateString(*r));
            r++;
        }
        cJSON_AddItemToObject(obj, "required", req);

    } else if (s->type == MCP_INPUT_SCHEMA_TYPE_ARRAY) {
        cJSON* items = NULL;
        if (s->properties && s->properties->type != MCP_INPUT_SCHEMA_TYPE_NULL) {
            items = mcp_input_schema_to_json(s->properties);
        } else {
            items = cJSON_CreateObject();
            cJSON_AddStringToObject(items, "type", schema_type_to_string(s->type_arr));
        }
        if (items) cJSON_AddItemToObject(obj, "items", items);
    }

    return obj;
}

static cJSON* handle_tools_list(mcp_server_t* server, cJSON* params) {
    (void)params;
    cJSON* tools = cJSON_CreateArray();

    for (int i = 0; i < server->tool_count; i++) {
        cJSON* tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", server->tools[i].name);
        cJSON_AddStringToObject(tool, "description",
            server->tools[i].description ? server->tools[i].description : "");

        cJSON* schema_json = mcp_input_schema_to_json(&server->tools[i].input_schema);
        if (schema_json) {
            cJSON_AddItemToObject(tool, "inputSchema", schema_json);
        }

        cJSON_AddItemToArray(tools, tool);
    }

    cJSON* response = cJSON_CreateObject();
    cJSON_AddItemToObject(response, "tools", tools);
    return response;
}

static cJSON* handle_tools_call(mcp_server_t* server, cJSON* params) {
    cJSON* name = cJSON_GetObjectItem(params, "name");
    cJSON* args = cJSON_GetObjectItem(params, "arguments");

    if (!name || !cJSON_IsString(name)) {
        return NULL;
    }

    for (int i = 0; i < server->tool_count; i++) {
        if (strcmp(server->tools[i].name, name->valuestring) == 0) {
            mcp_content_array_t* contents = mcp_content_array_create();
            if (!contents) return NULL;

            int err = server->tools[i].handler(args, contents);
            if (err != MCP_ERROR_NONE || contents->count == 0) {
                mcp_content_array_free(contents);
                return NULL;
            }

            cJSON* result = cJSON_CreateObject();
            cJSON* content = cJSON_CreateArray();
            cJSON_AddItemToObject(result, "content", content);

            for (int j = 0; j < contents->count; j++) {
                mcp_content_item_t* it = &contents->items[j];
                cJSON* content_obj = cJSON_CreateObject();
                if (it->type == MCP_CONTENT_TYPE_TEXT) {
                    cJSON_AddStringToObject(content_obj, "type", "text");
                    cJSON_AddStringToObject(content_obj, "text", it->text ? it->text : "");
                } else if (it->type == MCP_CONTENT_TYPE_IMAGE) {
                    cJSON_AddStringToObject(content_obj, "type", "image");
                    cJSON_AddStringToObject(content_obj, "data", it->data ? it->data : "");
                    cJSON_AddStringToObject(content_obj, "mimeType", it->mime_type ? it->mime_type : "");
                } else {
                    cJSON_AddStringToObject(content_obj, "type", "unknown");
                }
                cJSON_AddItemToArray(content, content_obj);
            }

            mcp_content_array_free(contents);
            return result;
        }
    }

    return NULL;
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

static void handle_notifications_initialized(mcp_server_t* server, cJSON* params) {
    (void)server;
    (void)params;
    /* Client has finished initialization. No action needed for now. */
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

    const char* method_name = method->valuestring;
    int is_notification = (id == NULL);

    /* Handle notifications (no id, no response) */
    if (strcmp(method_name, "notifications/initialized") == 0) {
        handle_notifications_initialized(server, params);
        cJSON_Delete(request);
        *response = NULL;
        return MCP_ERROR_NONE;
    }

    /* Handle requests (have id, need response) */
    cJSON* result = NULL;
    int error = MCP_ERROR_NONE;

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
        /* Unknown method - only error if it's a request, ignore notifications */
        if (is_notification) {
            cJSON_Delete(request);
            *response = NULL;
            return MCP_ERROR_NONE;
        }
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
        char* json_str = read_jsonrpc_message(stdin);
        if (!json_str) {
            break;
        }

        fprintf(stderr, "CLI: %s\n", json_str);

        char* response = NULL;
        process_message(server, json_str, &response);

        if (response) {
            fprintf(stderr, "SRV: %s\n", response);
            /* Use helper to write JSON-RPC response and flush */
            if (write_jsonrpc_message(stdout, response) != 0) {
                fprintf(stderr, "mcp: failed to write response\n");
            }
            free(response);
        }

        free(json_str);
    }

    return MCP_ERROR_NONE;
}

/* Content array implementation */
mcp_content_array_t* mcp_content_array_create(void) {
    mcp_content_array_t* array = malloc(sizeof(mcp_content_array_t));
    if (!array) return NULL;
    array->count = 0;
    array->capacity = 4;
    array->items = calloc(array->capacity, sizeof(mcp_content_item_t));
    if (!array->items) {
        free(array);
        return NULL;
    }
    return array;
}

void mcp_content_array_free(mcp_content_array_t* array) {
    if (!array) return;
    for (int i = 0; i < array->count; i++) {
        sdsfree(array->items[i].text);
        sdsfree(array->items[i].data);
        sdsfree(array->items[i].mime_type);
    }
    free(array->items);
    free(array);
}

static int mcp_content_array_ensure(mcp_content_array_t* array) {
    if (array->count < array->capacity) return MCP_ERROR_NONE;
    int newcap = array->capacity * 2;
    mcp_content_item_t* n = realloc(array->items, newcap * sizeof(mcp_content_item_t));
    if (!n) return MCP_ERROR_OUT_OF_MEMORY;
    array->items = n;
    array->capacity = newcap;
    return MCP_ERROR_NONE;
}

int mcp_content_add_text(mcp_content_array_t* array, sds text) {
    if (!array || !text) {
        sdsfree(text);
        return MCP_ERROR_INVALID_ARGUMENT;
    }
    int err = mcp_content_array_ensure(array);
    if (err != MCP_ERROR_NONE) { sdsfree(text); return err; }
    mcp_content_item_t* it = &array->items[array->count++];
    it->type = MCP_CONTENT_TYPE_TEXT;
    it->text = text;
    it->data = NULL;
    it->mime_type = NULL;
    return MCP_ERROR_NONE;
}

int mcp_content_add_textf(mcp_content_array_t* array, const char* fmt, ...) {
    if (!array || !fmt) return MCP_ERROR_INVALID_ARGUMENT;
    va_list ap;
    va_start(ap, fmt);
    sds s = sdsempty();
    s = sdscatvprintf(s, fmt, ap);
    va_end(ap);
    if (!s) return MCP_ERROR_OUT_OF_MEMORY;
    return mcp_content_add_text(array, s);
}

int mcp_content_add_image(mcp_content_array_t* array, sds data, sds mime_type) {
    if (!array || !data || !mime_type) {
        sdsfree(data);
        sdsfree(mime_type);
        return MCP_ERROR_INVALID_ARGUMENT;
    }
    int err = mcp_content_array_ensure(array);
    if (err != MCP_ERROR_NONE) { sdsfree(data); sdsfree(mime_type); return err; }
    mcp_content_item_t* it = &array->items[array->count++];
    it->type = MCP_CONTENT_TYPE_IMAGE;
    it->text = NULL;
    it->data = data;
    it->mime_type = mime_type;
    return MCP_ERROR_NONE;
}

int mcp_server_serve(mcp_server_t* server, const char* address, int port) {
    (void)server;
    (void)address;
    (void)port;
    return MCP_ERROR_NOT_IMPLEMENTED;
}
