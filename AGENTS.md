# libmcp - Agent Coding Guidelines

## Project Overview
Minimal MCP (Model Context Protocol) library in C. Design goal: single header file + single source file with minimal external dependencies.

## Build Commands
```bash
# Build the example
make

# Clean build artifacts
make clean

# Run the example
./build/hello
```

## Linting
```bash
# Static analysis with cppcheck
cppcheck --enable=all --suppress=missingIncludeSystem *.c

# Scan with clang-analyzer
scan-build gcc -c libmcp.c cJSON.c
```

## Code Style Guidelines

### File Organization
- `libmcp.h` - Public API header (everything public, no internal headers)
- `libmcp.c` - Implementation (all code in this file)
- `examples/` - Example programs
- `tests/` - Test files

### Naming Conventions
- **Functions**: `mcp_lowercase_snake_case()` - prefix all public functions with `mcp_`
- **Types**: `McpTypeName` - CamelCase, Mcp prefix
- **Structs**: `mcp_type_name_t` - snake_case with _t suffix
- **Macros**: `MCP_UPPERCASE_SNAKE_CASE`
- **Constants**: `MCP_CONSTANT_NAME` (macros) or `mcp_constant` (static const)
- **Internal symbols**: `mcp_internal_func()` or `_mcp_internal` (non-public, though single-header design means most things are exposed)

### Includes
- All includes at top of .c file
- Standard library includes first (stdio, stdlib, string, etc.)
- No external dependencies beyond standard C library unless absolutely necessary
- Header file should have include guards: `#ifndef LIBMCP_H` and `#define LIBMCP_H`

### Types
- Use `size_t` for sizes and indices
- Use `int` for return codes (0 = success, negative = error)
- Use `void*` for opaque handles when needed
- Avoid `long long` and other platform-specific types unless needed
- Use `stdint.h` types (uint32_t, int64_t, etc.) for fixed-width integers

### Error Handling
- Functions return `int` with 0 on success, negative error codes on failure
- Define error codes as enum or macros: `MCP_ERROR_NONE = 0`, `MCP_ERROR_OUT_OF_MEMORY = -1`, etc.
- Provide `mcp_error_string(int code)` to convert error codes to descriptions
- Always check return values from memory allocation, never dereference NULL
- Avoid `assert()` in production code - return error codes instead
- Use `errno` and `strerror()` for system errors

### Memory Management
- Caller owns memory returned by functions (document clearly)
- Provide cleanup functions: `mcp_free(thing)`
- Never allocate memory without corresponding free function
- Document ownership in function comments (who allocates, who frees)
- Prefer stack allocation for small, short-lived objects

### Formatting
- 4 space indentation (no tabs)
- Max line length: 80 characters
- Opening brace on same line for functions, on new line for structs
- Space after keywords: `if (x)`, `while (y)`, not `if(x)`
- Space around operators: `a = b + c`, not `a=b+c`
- Pointer placement: `int* ptr` or `int *ptr` (be consistent - prefer `int* ptr`)

### Comments
- Use `/** */` for function documentation (brief description, parameters, return value)
- Use `/* */` for longer explanations in implementation
- Minimal inline comments - prefer self-documenting code
- Document memory ownership in function docs
- Example:
```c
/**
 * Creates a new MCP connection.
 * @param host Server hostname
 * @param port Server port
 * @return MCP handle on success, NULL on failure (check errno)
 */
mcp_connection_t* mcp_connect(const char* host, int port);
```

### Function Design
- Keep functions small and focused (max ~50 lines)
- Pass context as first parameter: `mcp_do_thing(ctx, ...)`
- Use const for input parameters that shouldn't be modified: `const char*`
- Avoid global state - require context objects
- Provide both synchronous and async APIs where appropriate

## Project Structure
```
libmcp.h        # Public API header
libmcp.c        # Implementation
cJSON.h         # JSON library header
cJSON.c         # JSON library implementation
examples/
  hello.c       # Example usage
build/          # Build output (gitignored)
Makefile        # Build rules
```
