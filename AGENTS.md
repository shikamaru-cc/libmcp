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

## Code Style Guidelines

### File organization
- `libmcp.h` - public API header (single public header; avoid internal
  headers)
- `libmcp.c` - implementation (all implementation in this file)
- `examples/` - example programs
- `tests/` - unit and integration tests

### Naming conventions
- Functions: `mcp_lowercase_snake_case()`; public functions should use
  the `mcp_` prefix where appropriate.
- Types: `McpTypeName` (CamelCase) for structs and enum type names.
- Macros: `MCP_UPPERCASE_SNAKE_CASE`.
- Constants: `MCP_CONSTANT_NAME` (macros) or `mcp_constant` (static
  const symbols).

### Includes
- Place all `#include` directives at the top of `.c` files.
- Order: project/osdep-like headers first if present, then system
  headers `<...>`, then local/project headers `"..."`.
- Header files must have include guards: `#ifndef LIBMCP_H` /
  `#define LIBMCP_H` / `#endif`.

### Types and pointers
- Use `size_t` for sizes and indices; use fixed-width types from
  `stdint.h` when a specific width is required.
- Use `int` for return codes (0 = success, negative = error) unless
  another convention is needed.
- Make pointers const-correct: use `const` on pointers that do not
  modify the referenced data.

### Error handling
- Prefer simple, consistent error conventions: 0 on success, negative
  on error, or non-null/null for pointer-returning functions.
- Define error codes as enums or macros (e.g. `MCP_ERROR_NONE = 0`).
- Provide `mcp_error_string(int code)` for human-readable messages.
- Do not use `assert()` for recoverable errors; return error codes.

### Memory management
- The caller owns memory returned from allocation functions unless
  documented otherwise; provide matching free functions like
  `mcp_free()`.
- Prefer stack allocation for short-lived, small objects.
- Prefer safer allocation helpers where available; document ownership
  in function comments.

### Formatting and brace style
- Use 4 spaces for indentation; do not use tabs (Makefiles are an
  exception).
- Aim for an 80 character line width; slightly exceed when it
  improves readability (linting tools may warn at 100).
- Always use braces for indented blocks, even if a block is a single
  statement. Put the opening brace on the same line as the control
  statement. For function definitions put the opening brace on a new
  line:

```
if (cond) {
    do_something();
}

void func(void)
{
    do_something();
}
```

### Declarations and conditionals
- Avoid mixing declarations and statements in the same block; prefer
  declarations at the start of a block. Declaring loop variables in
  `for` is acceptable.
- Write conditionals as `if (a == 1)` (constant on the right) rather
  than Yoda conditions.

### Comments
- Use C-style `/* ... */` comments for multi-line comments with a
  leading ` * ` column. Use `/** */` for function documentation (brief
  description, parameters, return value) and include ownership notes
  for returned memory.
- Avoid `//` comments to keep style consistent across tools.

Example:
```c
/**
 * Creates a new MCP connection.
 * @param host Server hostname
 * @param port Server port
 * @return MCP handle on success, NULL on failure (check errno)
 */
mcp_connection_t* mcp_connect(const char* host, int port);
```

### Strings, printing and safety
- Prefer `snprintf`/`vsnprintf` over `sprintf`/`vsprintf`.
- Prefer safe helpers for string copy/concat; avoid `strncpy` and
  `strcat`.
- When defining printf-like functions, annotate prototypes with
  `__attribute__((format(printf, ...)))` for compiler format checks.

### Automatic cleanup and helpers
- When available, prefer scope-based cleanup helpers (for example
  `g_autofree`/`g_autoptr` in GLib) to reduce explicit goto cleanup
  paths.

### QEMU-specific idioms (when applicable)
- If adopting QEMU patterns, follow QOM struct member names like
  `parent_obj`/`parent_class`, use QEMU GUARD macros for scoped
  resource handling (e.g. `QEMU_LOCK_GUARD`), and use QEMU error
  reporting APIs (`error_report`/`Error`) for consistent diagnostics.

Follow the full QEMU coding style page for additional details and
examples: https://qemu-project.gitlab.io/qemu/devel/style.html

## Project Structure
```
libmcp.h        # Public API header
libmcp.c        # Implementation
cJSON.h         # JSON library header
cJSON.c         # JSON library implementation
examples/
  hello.c       # Example usage
  redmine.c     # Redmine MCP implementation
build/          # Build output (gitignored)
Makefile        # Build rules
```
