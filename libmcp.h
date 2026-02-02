#ifndef LIBMCP_H
#define LIBMCP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MCP_INPUT_SCHEMA_TYPE_NULL   = 0,
    MCP_INPUT_SCHEMA_TYPE_NUMBER = 1 << 0,
    MCP_INPUT_SCHEMA_TYPE_STRING = 1 << 1,
    MCP_INPUT_SCHEMA_TYPE_BOOL   = 1 << 2,
    MCP_INPUT_SCHEMA_TYPE_ARRAY  = 1 << 3,
    MCP_INPUT_SCHEMA_TYPE_OBJECT = 1 << 4,
} McpInputSchemaTypeEnum;

typedef struct McpInputSchema {
    const char* name;
    const char* description;
    McpInputSchemaTypeEnum type;
    McpInputSchemaTypeEnum type_arr; /* only make sense for type == array */
    struct McpInputSchema* properties;
    const char** required;
} McpInputSchema;

#define mcp_input_schema_null { .type = MCP_INPUT_SCHEMA_TYPE_NULL }

typedef enum {
    MCP_CONTENT_TYPE_TEXT = 0,
    MCP_CONTENT_TYPE_IMAGE = 1,
    MCP_CONTENT_TYPE_RESOURCE = 2
} McpContentTypeEnum;

typedef struct McpContentItem {
    McpContentTypeEnum type;
    char* text;
    char* data;
    char* mime_type;
    struct McpContentItem* next;
} McpContentItem;

typedef struct McpToolCallResult {
    bool is_error;
    McpContentItem* head;
    McpContentItem* tail;
} McpToolCallResult;

typedef struct McpTool {
    const char* name;
    const char* description;
    McpInputSchema input_schema;
    McpToolCallResult* (*handler)(cJSON* params);
} McpTool;

McpToolCallResult* mcp_tool_call_result_create();
void mcp_tool_call_result_delete(McpToolCallResult*);
bool mcp_tool_call_result_add_text(McpToolCallResult*, const char* text);
bool mcp_tool_call_result_add_textf(McpToolCallResult*, const char* fmt, ...);
bool mcp_tool_call_result_add_image(McpToolCallResult*, const char* data, const char* mime_type);

static inline void mcp_tool_call_result_set_error(McpToolCallResult* r)
{
    r->is_error = true;
}

void mcp_add_tool(const McpTool* tool);
void mcp_set_name(const char* name);
void mcp_set_version(const char* version);

void mcp_main(int argc, const char** argv);

cJSON *cJSON_Select(cJSON *o, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
