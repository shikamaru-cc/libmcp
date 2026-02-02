#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "libmcp.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define MCP_MAX_TOOLS 128
#define MCP_MAX_PROMPTS 128
#define MCP_BUFFER_SIZE 8192

static const char* mcp_server_name = NULL;
static const char* mcp_server_version = NULL;
static McpTool mcp_server_tools[MCP_MAX_TOOLS];

static cJSON* jsonrpc_initialize(cJSON*);
static cJSON* jsonrpc_tools_list(cJSON*);
static cJSON* jsonrpc_tools_call(cJSON*);
static cJSON* jsonrpc_notifications_initialized(cJSON*);

typedef struct JsonrpcMethod {
    const char* name;
    cJSON* (*handler)(cJSON*);
} JsonrpcMethod;

static JsonrpcMethod jsonrpc_methods[] = {
    { "initialize", jsonrpc_initialize },
    { "tools/list", jsonrpc_tools_list },
    { "tools/call", jsonrpc_tools_call },
    { "notifications/initialized", jsonrpc_notifications_initialized },
    { NULL, NULL },
};

static char* read_jsonrpc_message(FILE* in)
{
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

static void write_jsonrpc_message(FILE* out, cJSON* json)
{
    char* s = cJSON_PrintUnformatted(json);
    if (!s) return;
    fprintf(out, "%s\n", s);
    fflush(out);
    free(s);
}

const char* mcp_error_string(int code)
{
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

void mcp_set_name(const char* name)
{
    mcp_server_name = name;
}

void mcp_set_version(const char* version)
{
    mcp_server_version = version;
}

void mcp_add_tool(const McpTool* tool)
{
    static int n = 0;
    if (n >= MCP_MAX_TOOLS) {
        fprintf(stderr, "Add more than %d tools, skip\n", MCP_MAX_TOOLS);
        return;
    }

    mcp_server_tools[n++] = *tool;
}

/* Prompt API removed in this build */

static cJSON* jsonrpc_initialize(cJSON* params)
{
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

    cJSON_AddItemToObject(response, "capabilities", capabilities);

    /* Server info */
    cJSON* server_info = cJSON_CreateObject();
    cJSON_AddStringToObject(server_info, "name", mcp_server_name);
    cJSON_AddStringToObject(server_info, "version", mcp_server_version);
    cJSON_AddItemToObject(response, "serverInfo", server_info);

    return response;
}

static const char* schema_type_to_string(mcp_input_schema_type_e t)
{
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

/* Convert internal McpInputSchema to a cJSON object representing the schema.
   Returns a new cJSON object or NULL if schema is null/empty. */
static cJSON* mcp_input_schema_marshal(const McpInputSchema* s)
{
    if (!s) return NULL;
    if (s->type == MCP_INPUT_SCHEMA_TYPE_NULL) return NULL;

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", schema_type_to_string(s->type));

    if (s->description) {
        cJSON_AddStringToObject(obj, "description", s->description);
    }

    if (s->type == MCP_INPUT_SCHEMA_TYPE_OBJECT) {
        cJSON* props = cJSON_CreateObject();
        const McpInputSchema* p = s->properties;
        while (p && p->type != MCP_INPUT_SCHEMA_TYPE_NULL) {
            cJSON* prop_schema = mcp_input_schema_marshal(p);
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
            items = mcp_input_schema_marshal(s->properties);
        } else {
            items = cJSON_CreateObject();
            cJSON_AddStringToObject(items, "type", schema_type_to_string(s->type_arr));
        }
        if (items) cJSON_AddItemToObject(obj, "items", items);
    }

    return obj;
}

static cJSON* jsonrpc_tools_list(cJSON* params)
{
    (void)params;
    cJSON* response = cJSON_CreateObject();
    cJSON* tools = cJSON_AddArrayToObject(response, "tools");
    for (McpTool* i = mcp_server_tools; i->name; i++) {
        cJSON* tool = cJSON_CreateObject();
        cJSON_AddItemToArray(tools, tool);

        cJSON_AddStringToObject(tool, "name", i->name);
        cJSON_AddStringToObject(tool, "description", i->description);
        cJSON* schema_json = mcp_input_schema_marshal(&i->input_schema);
        if (schema_json)
            cJSON_AddItemToObject(tool, "inputSchema", schema_json);
    }
    return response;
}

static cJSON* jsonrpc_tools_call(cJSON* params)
{
    cJSON* name = cJSON_Select(params, ".name:s");
    if (!name)
        return NULL;

    cJSON* args = cJSON_GetObjectItem(params, "arguments");

    for (McpTool* i = mcp_server_tools; i->name; i++) {
        if (strcmp(i->name, name->valuestring) != 0)
            continue;

        McpToolCallResult* result = i->handler(args);
        if (!result)
            return NULL;

        cJSON* result_obj = cJSON_CreateObject();
        cJSON* content = cJSON_AddArrayToObject(result_obj, "content");

        McpContentItem* it;
        for (it = result->head; it != NULL; it = it->next) {
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

        mcp_tool_call_result_delete(result);
        return result_obj;
    }

    return NULL;
}

static cJSON* jsonrpc_notifications_initialized(cJSON* params)
{
    (void)params;
    return NULL;
}

static cJSON* handle_request(cJSON* request)
{
    cJSON* method = cJSON_Select(request, ".method:s");
    if (!method)
        return NULL;

    cJSON* id = cJSON_GetObjectItem(request, "id");
    cJSON* params = cJSON_GetObjectItem(request, "params");

    for (JsonrpcMethod* i = jsonrpc_methods; i->name; i++) {
        if (strcmp(method->valuestring, i->name) != 0)
            continue;

        cJSON* result = i->handler(params);
        if (!result)
            return NULL;

        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "jsonrpc", "2.0");
        cJSON_AddItemReferenceToObject(response, "id", id);
        cJSON_AddItemToObject(response, "result", result);
        return response;
    }

    /* TODO: return error message */
    return NULL;
}

void mcp_main(int argc, const char** argv)
{
    (void)argc;
    (void)argv;

    while (1) {
        char* message = read_jsonrpc_message(stdin);
        if (!message)
            break;

        cJSON* request = cJSON_Parse(message);
        free(message);
        if (!request)
            continue;

        cJSON* response = handle_request(request);
        if (response) {
            write_jsonrpc_message(stdout, response);
            free(response);
        }

        free(request);
    }
}

McpToolCallResult* mcp_tool_call_result_create()
{
    McpToolCallResult* r = malloc(sizeof(McpToolCallResult));
    if (r == NULL)
        return NULL;

    r->is_error = false;
    r->head = NULL;
    r->tail = NULL;
    return r;
}

static void mcp_tool_call_result_add_content(McpToolCallResult* r, McpContentItem* i)
{
    i->next = NULL;

    if (r->head == NULL)
        r->head = i;

    if (r->tail == NULL)
        r->tail = i;
    else
        r->tail->next = i;
}

static void mcp_content_item_delete(McpContentItem* i)
{
    if (i == NULL) return;
    mcp_content_item_delete(i->next);
    free(i);
}

void mcp_tool_call_result_delete(McpToolCallResult* r)
{
    if (r == NULL) return;
    mcp_content_item_delete(r->head);
    free(r);
}

bool mcp_tool_call_result_add_text(McpToolCallResult* r, const char* text)
{
    McpContentItem* i = (McpContentItem*)malloc(sizeof(McpContentItem));
    if (i == NULL)
        return false;

    i->type = MCP_CONTENT_TYPE_TEXT;
    i->text = strdup(text);
    i->data = NULL;
    i->mime_type = NULL;

    mcp_tool_call_result_add_content(r, i);
    return true;
}

bool mcp_tool_call_result_add_textf(McpToolCallResult* r, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char* s = NULL;
    int len = vasprintf(&s, fmt, ap);
    va_end(ap);
    if (len < 0) return false;
    bool rc = mcp_tool_call_result_add_text(r, s);
    free(s);
    return rc;
}

bool mcp_tool_call_result_add_image(McpToolCallResult* r, const char* data, const char* mime_type)
{
    McpContentItem* i = (McpContentItem*)malloc(sizeof(McpContentItem));
    if (i == NULL)
        return false;

    i->type = MCP_CONTENT_TYPE_IMAGE;
    i->text = NULL;
    i->data = strdup(data);
    i->mime_type = strdup(mime_type);

    mcp_tool_call_result_add_content(r, i);
    return true;
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
cJSON *cJSON_Select(cJSON *o, const char *fmt, ...)
{
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
