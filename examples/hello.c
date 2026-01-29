#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libmcp.h"
#include "cJSON.h"

static cJSON* add_handler(cJSON* params) {
    cJSON* a = cJSON_GetObjectItem(params, "a");
    cJSON* b = cJSON_GetObjectItem(params, "b");
    int ai = cJSON_IsNumber(a) ? a->valueint : 0;
    int bi = cJSON_IsNumber(b) ? b->valueint : 0;
    cJSON* result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "result", ai + bi);
    return result;
}

static cJSON* multiply_handler(cJSON* params) {
    cJSON* a = cJSON_GetObjectItem(params, "a");
    cJSON* b = cJSON_GetObjectItem(params, "b");
    int ai = cJSON_IsNumber(a) ? a->valueint : 0;
    int bi = cJSON_IsNumber(b) ? b->valueint : 0;
    cJSON* result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "result", ai * bi);
    return result;
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

static int prompt_handler(const char* json_args, char** prompt_text, void* user_data) {
    (void)user_data;
    (void)json_args;

    *prompt_text = strdup("This is a sample prompt template. You can use it as a starting point.");
    return *prompt_text ? 0 : MCP_ERROR_OUT_OF_MEMORY;
}

int main(void) {
    mcp_server_t* server = mcp_server_create();
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    mcp_server_set_name(server, "example-calculator");
    mcp_server_set_version(server, "1.0.0");

    mcp_server_register_tool(server, &tool_add);
    mcp_server_register_tool(server, &tool_multiply);

    mcp_prompt_t sample_prompt = {"sample", "A sample prompt template"};
    mcp_server_register_prompt(server, &sample_prompt, prompt_handler, NULL);

    fprintf(stderr, "MCP Calculator Server running...\n");
    fprintf(stderr, "Available tools: add, multiply\n");
    fprintf(stderr, "Available prompts: sample\n");
    fprintf(stderr, "Press Ctrl+C to stop\n");

    int result = mcp_server_serve_stdio(server);

    mcp_server_destroy(server);
    return result;
}
