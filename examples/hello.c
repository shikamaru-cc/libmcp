#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libmcp.h"

static int add_handler(const char* json_args, char** json_result, void* user_data) {
    (void)user_data;

    int a = 0, b = 0;
    sscanf(json_args, "{\"a\":%d,\"b\":%d}", &a, &b);

    char result[128];
    snprintf(result, sizeof(result), "{\"result\":%d}", a + b);

    *json_result = strdup(result);
    return *json_result ? 0 : MCP_ERROR_OUT_OF_MEMORY;
}

static int multiply_handler(const char* json_args, char** json_result, void* user_data) {
    (void)user_data;

    int a = 0, b = 0;
    sscanf(json_args, "{\"a\":%d,\"b\":%d}", &a, &b);

    char result[128];
    snprintf(result, sizeof(result), "{\"result\":%d}", a * b);

    *json_result = strdup(result);
    return *json_result ? 0 : MCP_ERROR_OUT_OF_MEMORY;
}

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

    mcp_tool_t add_tool = {"add", "Add two numbers"};
    mcp_tool_t multiply_tool = {"multiply", "Multiply two numbers"};

    mcp_server_register_tool(server, &add_tool, add_handler, NULL);
    mcp_server_register_tool(server, &multiply_tool, multiply_handler, NULL);

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
