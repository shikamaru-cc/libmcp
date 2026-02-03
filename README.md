# libmcp

A minimal **MCP (Model Context Protocol)** library in C.

## Design Goals

- **Single header + single source**: Just `libmcp.h` and `libmcp.c`
- **Minimal dependencies**: Uses only lightweight libraries (cJSON, sds, stb)
- **Simple API**: Easy to create MCP tools and servers
- **Type-safe**: JSON-RPC parameter validation with type annotations

## Features

- JSON-RPC 2.0 protocol implementation
- Tool registration with schema validation
- stdin/stdout transport for communication with MCP clients
- Resource and prompt support
- Content types: text, images, and resources
- Example implementations for Redmine and HackerNews

## Building

```bash
# Build all examples
make

# Build specific example
make hello
make redmine
make hackernews

# Clean build artifacts
make clean
```

## Dependencies

- `cJSON` - JSON parsing/generation (included)
- `sds` - Dynamic strings (included)
- `stb` - Utility libraries (included)
- `libcurl` - Required for Redmine/HackerNews examples

## Example Usage

```c
#include "libmcp.h"
#include "cJSON.h"

static McpToolCallResult* add_handler(cJSON* params)
{
    McpToolCallResult* r = mcp_tool_call_result_create();
    cJSON* a = cJSON_GetObjectItem(params, "a:n");
    cJSON* b = cJSON_GetObjectItem(params, "b:n");

    if (!a || !b) {
        mcp_tool_call_result_set_error(r);
        mcp_tool_call_result_add_text(r, "invalid params");
        return r;
    }

    mcp_tool_call_result_add_textf(r, "%d", a->valueint + b->valueint);
    return r;
}

int main(int argc, const char* argv[])
{
    mcp_set_name("my-mcp-server");
    mcp_set_version("1.0.0");

    static McpInputSchema schema[] = {
        { .name = "a", .type = MCP_INPUT_SCHEMA_TYPE_NUMBER },
        { .name = "b", .type = MCP_INPUT_SCHEMA_TYPE_NUMBER },
        mcp_input_schema_null
    };

    static McpTool tool = {
        .name = "add",
        .description = "Add two numbers",
        .handler = add_handler,
        .input_schema = {
            .type = MCP_INPUT_SCHEMA_TYPE_OBJECT,
            .properties = schema,
        },
    };

    mcp_add_tool(&tool);
    mcp_main(argc, argv);
    return 0;
}
```

## Examples

### hello
Basic example with simple tools (add, multiply, weather).

```bash
./build/hello
```

### redmine
MCP server for Redmine project management. Tools include:
- List projects, issues, wiki pages
- Create issues and add notes
- Search wiki and list activities
- Time entry tracking

Requires:
- `REDMINE_URL` - Redmine server URL
- `REDMINE_API_KEY` - API authentication key

```bash
export REDMINE_URL="https://redmine.example.com"
export REDMINE_API_KEY="your-api-key"
./build/redmine
```

### hackernews
MCP server for Hacker News. Browse stories and comments.

```bash
./build/hackernews
```

## API Reference

### Core Functions

- `mcp_set_name(const char* name)` - Set server name
- `mcp_set_version(const char* version)` - Set server version
- `mcp_add_tool(const McpTool* tool)` - Register a tool
- `mcp_main(int argc, const char** argv)` - Start MCP server

### Tool Handlers

Tool handlers receive `cJSON* params` and return `McpToolCallResult*`:

```c
typedef McpToolCallResult* (*McpToolHandler)(cJSON* params);
```

### Content Types

- `mcp_tool_call_result_add_text(result, "text")` - Add text content
- `mcp_tool_call_result_add_textf(result, "format %d", value)` - Formatted text
- `mcp_tool_call_result_add_image(result, data, mime_type)` - Add image
- `mcp_tool_call_result_set_error(result)` - Mark as error

### Input Schema

Define parameter types using `McpInputSchema`:

- `MCP_INPUT_SCHEMA_TYPE_NULL`
- `MCP_INPUT_SCHEMA_TYPE_NUMBER`
- `MCP_INPUT_SCHEMA_TYPE_STRING`
- `MCP_INPUT_SCHEMA_TYPE_BOOL`
- `MCP_INPUT_SCHEMA_TYPE_ARRAY`
- `MCP_INPUT_SCHEMA_TYPE_OBJECT`

Type annotations use `:type` suffix in parameter names:
- `param:n` - number
- `param:s` - string
- `param:b` - boolean
- `param:a` - array
- `param:o` - object

## Project Structure

```
libmcp.h         # Public API header
libmcp.c         # Implementation
cJSON.h/c        # JSON library
sds.h/c          # Dynamic strings
stb.h/c          # Utilities
examples/
  hello.c        # Basic example
  redmine.c      # Redmine integration
  hackernews.c   # HackerNews integration
```

## License

[Specify your license here]

## Contributing

Contributions welcome! Please see AGENTS.md for coding guidelines.

## Resources

- [Model Context Protocol Specification](https://spec.modelcontextprotocol.io/)
- [MCP SDK Documentation](https://modelcontextprotocol.io/docs)
