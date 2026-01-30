#ifndef LIBMCP_H
#define LIBMCP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct McpServer McpServer;
typedef struct McpConnection McpConnection;

typedef int mcp_error_code_t;

typedef enum {
    MCP_ERROR_NONE = 0,
    MCP_ERROR_INVALID_ARGUMENT = -1,
    MCP_ERROR_OUT_OF_MEMORY = -2,
    MCP_ERROR_NOT_FOUND = -3,
    MCP_ERROR_PROTOCOL = -4,
    MCP_ERROR_IO = -5,
    MCP_ERROR_NOT_IMPLEMENTED = -6
} mcp_error_t;

typedef enum {
    MCP_INPUT_SCHEMA_TYPE_NULL   = 0,
    MCP_INPUT_SCHEMA_TYPE_NUMBER = 1 << 0,
    MCP_INPUT_SCHEMA_TYPE_STRING = 1 << 1,
    MCP_INPUT_SCHEMA_TYPE_BOOL   = 1 << 2,
    MCP_INPUT_SCHEMA_TYPE_ARRAY  = 1 << 3,
    MCP_INPUT_SCHEMA_TYPE_OBJECT = 1 << 4,
} mcp_input_schema_type_e;

typedef struct McpInputSchema {
    const char* name;
    const char* description;
    mcp_input_schema_type_e type;
    mcp_input_schema_type_e type_arr; /* only make sense for type == array */
    struct McpInputSchema* properties;
    const char** required;
} McpInputSchema;

#define mcp_input_schema_null { .type = MCP_INPUT_SCHEMA_TYPE_NULL }

/* Tool handler returns int error code and fills provided content array.
 * The handler must return MCP_ERROR_NONE on success. */
typedef struct McpContentItem McpContentItem;
typedef struct McpContentArray McpContentArray;

typedef int (*mcp_tool_handler_t)(cJSON* params, McpContentArray* contents);

typedef struct {
    const char* name;
    const char* description;
    mcp_tool_handler_t handler;
    McpInputSchema input_schema;
} mcp_tool_t;

/* Content API */
typedef enum {
    MCP_CONTENT_TYPE_TEXT = 0,
    MCP_CONTENT_TYPE_IMAGE = 1,
    MCP_CONTENT_TYPE_RESOURCE = 2
} mcp_content_type_t;

struct McpContentItem {
    mcp_content_type_t type;
    char* text;      /* owned */
    char* data;      /* owned: image data (base64) or resource */
    char* mime_type; /* owned */
};

struct McpContentArray {
    McpContentItem* items;
    int count;
    int capacity;
};

/* Content array management */
McpContentArray* mcp_content_array_create(void);
void mcp_content_array_free(McpContentArray* array);

/* Add content items - libmcp makes copies of provided strings. */
int mcp_content_add_text(McpContentArray* array, const char* text);
int mcp_content_add_textf(McpContentArray* array, const char* fmt, ...);
int mcp_content_add_image(McpContentArray* array, const char* data, const char* mime_type);


/* Server lifecycle and registration */
McpServer* mcp_server_create(void);

void mcp_server_destroy(McpServer* server);
int mcp_server_set_name(McpServer* server, const char* name);
int mcp_server_set_version(McpServer* server, const char* version);
void mcp_server_register_tool(McpServer* server, const mcp_tool_t* tool);

int mcp_server_serve(McpServer* server, const char* address, int port);

int mcp_server_serve_stdio(McpServer* server);

const char* mcp_error_string(int code);

cJSON *cJSON_Select(cJSON *o, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
