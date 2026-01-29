#ifndef LIBMCP_H
#define LIBMCP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"
#include "sds.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_server mcp_server_t;
typedef struct mcp_connection mcp_connection_t;

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

typedef struct mcp_input_schema {
    const char* name;
    const char* description;
    mcp_input_schema_type_e type;
    mcp_input_schema_type_e type_arr; // only make sense for type == array
    struct mcp_input_schema* properties;
    const char** required;
} mcp_input_schema_t;

#define mcp_input_schema_null { .type = MCP_INPUT_SCHEMA_TYPE_NULL }

/* Tool handler returns int error code and fills provided content array.
 * The handler must return MCP_ERROR_NONE on success. */
typedef struct mcp_content_item mcp_content_item_t;
typedef struct mcp_content_array mcp_content_array_t;

typedef int (*mcp_tool_handler_t)(cJSON* params, mcp_content_array_t* contents);

typedef struct {
    const char* name;
    const char* description;
    mcp_tool_handler_t handler;
    mcp_input_schema_t input_schema;
} mcp_tool_t;

/* Content API */
typedef enum {
    MCP_CONTENT_TYPE_TEXT = 0,
    MCP_CONTENT_TYPE_IMAGE = 1,
    MCP_CONTENT_TYPE_RESOURCE = 2
} mcp_content_type_t;

struct mcp_content_item {
    mcp_content_type_t type;
    sds text;      /* owned */
    sds data;      /* owned: image data (base64) or resource */
    sds mime_type; /* owned */
};

struct mcp_content_array {
    mcp_content_item_t* items;
    int count;
    int capacity;
};

/* Content array management (libmcp takes ownership of passed sds strings) */
mcp_content_array_t* mcp_content_array_create(void);
void mcp_content_array_free(mcp_content_array_t* array);

/* Add content items - functions take ownership of provided sds strings. */
int mcp_content_add_text(mcp_content_array_t* array, sds text);
int mcp_content_add_textf(mcp_content_array_t* array, const char* fmt, ...);
int mcp_content_add_image(mcp_content_array_t* array, sds data, sds mime_type);


/* Server lifecycle and registration */
mcp_server_t* mcp_server_create(void);

void mcp_server_destroy(mcp_server_t* server);
int mcp_server_set_name(mcp_server_t* server, const char* name);
int mcp_server_set_version(mcp_server_t* server, const char* version);
void mcp_server_register_tool(mcp_server_t* server, const mcp_tool_t* tool);

int mcp_server_serve(mcp_server_t* server, const char* address, int port);

int mcp_server_serve_stdio(mcp_server_t* server);

const char* mcp_error_string(int code);

#ifdef __cplusplus
}
#endif

#endif
