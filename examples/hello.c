#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libmcp.h"
#include "cJSON.h"

static McpToolCallResult* add_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    cJSON* a = cJSON_GetObjectItem(params, "a:n");
    cJSON* b = cJSON_GetObjectItem(params, "b:n");
    if (!a || !b) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_textf(r, "invalid params");
        return r;
    }
    mcp_tool_call_result_add_textf(r, "%d", a->valueint + b->valueint);
    return r;
}

static McpToolCallResult* multiply_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    cJSON* a = cJSON_GetObjectItem(params, "a:n");
    cJSON* b = cJSON_GetObjectItem(params, "b:n");
    if (!a || !b) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_textf(r, "invalid params");
        return r;
    }
    mcp_tool_call_result_add_textf(r, "%d", a->valueint + b->valueint);
    return r;
}

static McpToolCallResult* weather_handler(cJSON* params)
{
    (void)params;
    McpToolCallResult* r = mcp_tool_call_result_create();
    mcp_tool_call_result_add_text(r, "sunny day baby");
    return r;
}

static McpInputSchema tool_add_schema[] = {
    { .name = "a",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "b",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static McpTool tool_add = {
    .name = "add",
    .description = "Add two numbers",
    .handler = add_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_add_schema,
    },
};

static McpInputSchema tool_multiply_schema[] = {
    { .name = "a",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "b",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static McpTool tool_multiply = {
    .name = "multiply",
    .description = "Multiply two numbers",
    .handler = multiply_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_multiply_schema,
    },
};

static McpInputSchema tool_weather_schema[] = {
    mcp_input_schema_null
};

static McpTool tool_weather = {
    .name = "weather",
    .description = "Show today's weather",
    .handler = weather_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_weather_schema,
    },
};

int main(void)
{
    McpServer* server = mcp_server_create();
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    mcp_server_set_name(server, "libmcp-sample");
    mcp_server_set_version(server, "1.0.0");

    mcp_server_register_tool(server, &tool_add);
    mcp_server_register_tool(server, &tool_multiply);
    mcp_server_register_tool(server, &tool_weather);

    /* prompts removed - prompt handler unused */

    fprintf(stderr, "MCP Example Server running...\n");

    int result = mcp_server_serve_stdio(server);

    mcp_server_destroy(server);
    return result;
}
