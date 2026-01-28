# libmcp

A Model Context Protocol (MCP) library implementation in pure C with minimal dependencies.

## Features

- **Pure C implementation** - Written in C99 with no external dependencies
- **Minimal footprint** - Small, focused codebase for easy integration
- **JSON-RPC 2.0** - Full JSON-RPC 2.0 message handling
- **stdio transport** - Communication over standard input/output
- **MCP protocol support** - Implements MCP initialization, capabilities, and request/response patterns
- **Simple API** - Easy-to-use callback-based API for handling requests and notifications

## Building

Build the library using make:

```bash
make
```

Build the examples:

```bash
make examples
```

Clean build artifacts:

```bash
make clean
```

## Installation

Install the library and headers system-wide:

```bash
sudo make install
```

This installs:
- `libmcp.a` to `/usr/local/lib/`
- Header files to `/usr/local/include/`

## Usage

### Basic Server Example

```c
#include <stdio.h>
#include "mcp.h"

/* Request handler callback */
char* my_request_handler(const char *method, const char *params, void *user_data) {
    /* Handle the request and return a JSON result string */
    return strdup("{\"result\":\"success\"}");
}

int main(void) {
    /* Create server */
    mcp_server_t *server = mcp_server_create("my-server", "1.0.0");
    
    /* Set capabilities */
    mcp_capabilities_t caps = {0};
    caps.supports_tools = 1;
    mcp_server_set_capabilities(server, caps);
    
    /* Set request handler */
    mcp_server_set_request_handler(server, my_request_handler, NULL);
    
    /* Run server (blocks) */
    mcp_server_run(server);
    
    /* Cleanup */
    mcp_server_destroy(server);
    return 0;
}
```

### Compiling Your Program

```bash
gcc -o my_server my_server.c -L. -lmcp -Iinclude
```

## API Reference

### Server Creation and Management

- `mcp_server_t* mcp_server_create(const char *name, const char *version)` - Create a new MCP server
- `void mcp_server_destroy(mcp_server_t *server)` - Destroy server and free resources
- `void mcp_server_set_capabilities(mcp_server_t *server, mcp_capabilities_t capabilities)` - Set server capabilities
- `int mcp_server_run(mcp_server_t *server)` - Run the server event loop (blocking)

### Request/Notification Handlers

- `void mcp_server_set_request_handler(mcp_server_t *server, mcp_request_handler_t handler, void *user_data)` - Set request handler callback
- `void mcp_server_set_notification_handler(mcp_server_t *server, mcp_notification_handler_t handler, void *user_data)` - Set notification handler callback

### Message Handling

- `mcp_message_t* mcp_message_parse(const char *json)` - Parse JSON-RPC message
- `char* mcp_message_serialize(const mcp_message_t *message)` - Serialize message to JSON
- `void mcp_message_free(mcp_message_t *message)` - Free message structure
- `mcp_message_t* mcp_create_error_response(mcp_id_t id, int id_is_string, int code, const char *message)` - Create error response
- `mcp_message_t* mcp_create_response(mcp_id_t id, int id_is_string, const char *result)` - Create success response

### Server Notifications

- `int mcp_server_send_notification(mcp_server_t *server, const char *method, const char *params)` - Send notification to client

## Examples

### Echo Server

A simple server that echoes back any request:

```bash
./examples/echo_server
```

Test with:
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./examples/echo_server
```

### Tool Server

A calculator server providing add and multiply tools:

```bash
./examples/tool_server
```

Test the initialize handshake:
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./examples/tool_server
```

List available tools:
```bash
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | ./examples/tool_server
```

Call the add tool:
```bash
echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"add","arguments":{"a":5,"b":3}}}' | ./examples/tool_server
```

## Protocol Support

libmcp implements the Model Context Protocol specification:

- **JSON-RPC 2.0** message format
- **Initialize handshake** with capability negotiation
- **Request/Response** pattern for RPC calls
- **Notifications** for one-way messages
- **stdio transport** for local communication

### Supported Methods

The library handles the `initialize` method automatically. Other methods are dispatched to your request handler callback.

### Capabilities

Servers can declare support for:
- `tools` - Executable functions
- `resources` - Data sources
- `prompts` - Prompt templates
- `logging` - Log message forwarding

## Dependencies

**None!** This library is pure C99 with no external dependencies. It uses only the C standard library.

## License

MIT License - See LICENSE file for details

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

## MCP Protocol

For more information about the Model Context Protocol, visit:
- [MCP Specification](https://modelcontextprotocol.io/)
- [Anthropic MCP Documentation](https://docs.anthropic.com/en/docs/mcp)