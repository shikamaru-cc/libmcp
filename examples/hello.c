#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../libmcp.h"
#include "../cJSON.h"

static int add_handler(cJSON* params, mcp_content_array_t* contents) {
    cJSON* a = cJSON_GetObjectItem(params, "a");
    cJSON* b = cJSON_GetObjectItem(params, "b");
    int ai = cJSON_IsNumber(a) ? a->valueint : 0;
    int bi = cJSON_IsNumber(b) ? b->valueint : 0;
    return mcp_content_add_textf(contents, "%d", ai + bi);
}

static int multiply_handler(cJSON* params, mcp_content_array_t* contents) {
    cJSON* a = cJSON_GetObjectItem(params, "a");
    cJSON* b = cJSON_GetObjectItem(params, "b");
    int ai = cJSON_IsNumber(a) ? a->valueint : 0;
    int bi = cJSON_IsNumber(b) ? b->valueint : 0;
    return mcp_content_add_textf(contents, "%d", ai * bi);
}

static int weather_handler(cJSON* params, mcp_content_array_t* contents) {
    (void)params;
    return mcp_content_add_text(contents, sdsnew("sunny day baby"));
}

static mcp_input_schema_t tool_add_schema[] = {
    { .name = "a",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "b",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static mcp_tool_t tool_add = {
    .name = "add",
    .description = "Add two numbers",
    .handler = add_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_add_schema,
    },
};

static mcp_input_schema_t tool_multiply_schema[] = {
    { .name = "a",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    { .name = "b",
      .type = MCP_INPUT_SCHEMA_TYPE_NUMBER,
    },
    mcp_input_schema_null
};

static mcp_tool_t tool_multiply = {
    .name = "multiply",
    .description = "Multiply two numbers",
    .handler = multiply_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_multiply_schema,
    },
};

static mcp_input_schema_t tool_weather_schema[] = {
    mcp_input_schema_null
};

static mcp_tool_t tool_weather = {
    .name = "weather",
    .description = "Show today's weather",
    .handler = weather_handler,
    .input_schema = {
        .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
        .properties = tool_weather_schema,
    },
};

int main(void) {
    mcp_server_t* server = mcp_server_create();
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
