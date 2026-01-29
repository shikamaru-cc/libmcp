#ifndef LIBMCP_H
#define LIBMCP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

typedef struct {
    const char* name;
    const char* description;
} mcp_tool_t;

typedef struct {
    const char* name;
    const char* description;
} mcp_prompt_t;

typedef int (*mcp_tool_handler_t)(
    const char* json_args,
    char** json_result,
    void* user_data
);

typedef int (*mcp_prompt_handler_t)(
    const char* json_args,
    char** prompt_text,
    void* user_data
);

mcp_server_t* mcp_server_create(void);

void mcp_server_destroy(mcp_server_t* server);

int mcp_server_set_name(mcp_server_t* server, const char* name);

int mcp_server_set_version(mcp_server_t* server, const char* version);

int mcp_server_register_tool(
    mcp_server_t* server,
    const mcp_tool_t* tool,
    mcp_tool_handler_t handler,
    void* user_data
);

int mcp_server_register_prompt(
    mcp_server_t* server,
    const mcp_prompt_t* prompt,
    mcp_prompt_handler_t handler,
    void* user_data
);

int mcp_server_serve(mcp_server_t* server, const char* address, int port);

int mcp_server_serve_stdio(mcp_server_t* server);

const char* mcp_error_string(int code);

#ifdef __cplusplus
}
#endif

#endif
