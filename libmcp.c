#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "libmcp.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define MCP_MAX_TOOLS 128
#define MCP_MAX_PROMPTS 128
#define MCP_BUFFER_SIZE 8192


struct mcp_server {
    char* name;
    char* version;
    int tool_count;
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

/* Prompt API removed in this build */

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

    /* Prompts capability removed */

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

/* Prompts removed from this minimal implementation */
static cJSON* handle_prompts_list(mcp_server_t* server, cJSON* params) {
    (void)server; (void)params;
    return NULL;
}

static void handle_notifications_initialized(mcp_server_t* server, cJSON* params) {
    (void)server;
    (void)params;
    /* Client has finished initialization. No action needed for now. */
}

static cJSON* handle_prompts_get(mcp_server_t* server, cJSON* params) {
    (void)server; (void)params;
    return NULL;
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

        char* response = NULL;
        process_message(server, json_str, &response);

        if (response) {
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
        free(array->items[i].text);
        free(array->items[i].data);
        free(array->items[i].mime_type);
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

int mcp_content_add_text(mcp_content_array_t* array, const char* text) {
    if (!array || !text) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }
    int err = mcp_content_array_ensure(array);
    if (err != MCP_ERROR_NONE) { return err; }
    mcp_content_item_t* it = &array->items[array->count++];
    it->type = MCP_CONTENT_TYPE_TEXT;
    it->text = strdup(text);
    it->data = NULL;
    it->mime_type = NULL;
    if (!it->text) {
        array->count--;
        return MCP_ERROR_OUT_OF_MEMORY;
    }
    return MCP_ERROR_NONE;
}

int mcp_content_add_textf(mcp_content_array_t* array, const char* fmt, ...) {
    if (!array || !fmt) return MCP_ERROR_INVALID_ARGUMENT;
    va_list ap;
    va_start(ap, fmt);
    char* s = NULL;
    int len = vasprintf(&s, fmt, ap);
    va_end(ap);
    if (len < 0 || !s) return MCP_ERROR_OUT_OF_MEMORY;
    int err = mcp_content_add_text(array, s);
    free(s);
    return err;
}

int mcp_content_add_image(mcp_content_array_t* array, const char* data, const char* mime_type) {
    if (!array || !data || !mime_type) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }
    int err = mcp_content_array_ensure(array);
    if (err != MCP_ERROR_NONE) { return err; }
    mcp_content_item_t* it = &array->items[array->count++];
    it->type = MCP_CONTENT_TYPE_IMAGE;
    it->text = NULL;
    it->data = strdup(data);
    it->mime_type = strdup(mime_type);
    if (!it->data || !it->mime_type) {
        free(it->data);
        free(it->mime_type);
        array->count--;
        return MCP_ERROR_OUT_OF_MEMORY;
    }
    return MCP_ERROR_NONE;
}

int mcp_server_serve(mcp_server_t* server, const char* address, int port) {
    (void)server;
    (void)address;
    (void)port;
    return MCP_ERROR_NOT_IMPLEMENTED;
}

/* JSON selector made by antirez.
 *
 * You can select things like this:
 *
 * cJSON *json = cJSON_Parse(myjson_string);
 * cJSON *width = cJSON_Select(json,".features.screens[*].width",4);
 * cJSON *height = cJSON_Select(json,".features.screens[4].*","height");
 * cJSON *price = cJSON_Select(json,".features.screens[4].price_*",
 *                  price_type == EUR ? "eur" : "usd");
 *
 * You can use a ":<type>" specifier, usually at the end, in order to
 * check the type of the final JSON object selected. If the type will not
 * match, the function will return NULL. For instance the specifier:
 *
 *  ".foo.bar:s"
 *
 * Will not return NULL only if the root object has a foo field, that is
 * an object with a bat field, that contains a string. This is the full
 * list of selectors:
 *
 *  ".field", select the "field" of the current object.
 *  "[1234]", select the specified index of the current array.
 *  ":<type>", check if the currently selected type is of the specified type,
 *             where the type is a single letter that can be:
 *             "s" for string
 *             "n" for number
 *             "a" for array
 *             "o" for object
 *             "b" for boolean
 *             "!" for null
 *
 * Selectors can be combined, and the special "*" can be used in order to
 * fetch array indexes or field names from the arguments:
 *
 *      cJSON *myobj = cJSON_Select(root,".properties[*].*", index, fieldname);
 */
#define JSEL_INVALID 0
#define JSEL_OBJ 1            /* "." */
#define JSEL_ARRAY 2          /* "[" */
#define JSEL_TYPECHECK 3      /* ":" */
#define JSEL_MAX_TOKEN 256
cJSON *cJSON_Select(cJSON *o, const char *fmt, ...) {
    int next = JSEL_INVALID;        /* Type of the next selector. */
    char token[JSEL_MAX_TOKEN+1];   /* Current token. */
    int tlen;                       /* Current length of the token. */
    va_list ap;

    va_start(ap,fmt);
    const char *p = fmt;
    tlen = 0;
    while(1) {
        /* Our four special chars (plus the end of the string) signal the
         * end of the previous token and the start of the next one. */
        if (tlen && (*p == '\0' || strchr(".[]:",*p))) {
            token[tlen] = '\0';
            if (next == JSEL_INVALID) {
                goto notfound;
            } else if (next == JSEL_ARRAY) {
                if (!cJSON_IsArray(o)) goto notfound;
                int idx = atoi(token); /* cJSON API index is int. */
                if ((o = cJSON_GetArrayItem(o,idx)) == NULL)
                    goto notfound;
            } else if (next == JSEL_OBJ) {
                if (!cJSON_IsObject(o)) goto notfound;
                if ((o = cJSON_GetObjectItemCaseSensitive(o,token)) == NULL)
                    goto notfound;
            } else if (next == JSEL_TYPECHECK) {
                if (token[0] == 's' && !cJSON_IsString(o)) goto notfound;
                if (token[0] == 'n' && !cJSON_IsNumber(o)) goto notfound;
                if (token[0] == 'o' && !cJSON_IsObject(o)) goto notfound;
                if (token[0] == 'a' && !cJSON_IsArray(o)) goto notfound;
                if (token[0] == 'b' && !cJSON_IsBool(o)) goto notfound;
                if (token[0] == '!' && !cJSON_IsNull(o)) goto notfound;
            }
        } else if (next != JSEL_INVALID) {
            /* Otherwise accumulate characters in the current token, note that
             * the above check for JSEL_NEXT_INVALID prevents us from
             * accumulating at the start of the fmt string if no token was
             * yet selected. */
            if (*p != '*') {
                token[tlen] = *p++;
                tlen++;
                if (tlen > JSEL_MAX_TOKEN) goto notfound;
                continue;
            } else {
                /* The "*" character is special: if we are in the context
                 * of an array, we read an integer from the variable argument
                 * list, then concatenate it to the current string.
                 *
                 * If the context is an object, we read a string pointer
                 * from the variable argument string and concatenate the
                 * string to the current token. */
                int len;
                char buf[64];
                char *s;
                if (next == JSEL_ARRAY) {
                    int idx = va_arg(ap,int);
                    len = snprintf(buf,sizeof(buf),"%d",idx);
                    s = buf;
                } else if (next == JSEL_OBJ) {
                    s = va_arg(ap,char*);
                    len = strlen(s);
                } else {
                    goto notfound;
                }
                /* Common path. */
                if (tlen+len > JSEL_MAX_TOKEN) goto notfound;
                memcpy(token+tlen,s,len);
                tlen += len;
                p++;
                continue;
            }
        }
        /* Select the next token type according to its type specifier. */
        if (*p == ']') p++; /* Skip closing "]", it's just useless syntax. */
        if (*p == '\0') break;
        else if (*p == '.') next = JSEL_OBJ;
        else if (*p == '[') next = JSEL_ARRAY;
        else if (*p == ':') next = JSEL_TYPECHECK;
        else goto notfound;
        tlen = 0; /* A new token starts. */
        p++; /* Token starts at next character. */
    }

cleanup:
    va_end(ap);
    return o;

notfound:
    o = NULL;
    goto cleanup;
}
