/**
 * @file mcp_types.h
 * @brief MCP (Model Context Protocol) type definitions
 * 
 * Core types and structures for the MCP protocol implementation.
 */

#ifndef MCP_TYPES_H
#define MCP_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* JSON-RPC 2.0 protocol version */
#define MCP_JSONRPC_VERSION "2.0"

/* MCP protocol version */
#define MCP_PROTOCOL_VERSION "2024-11-05"

/**
 * MCP error codes (JSON-RPC 2.0 error codes)
 */
typedef enum {
    MCP_ERROR_PARSE_ERROR = -32700,
    MCP_ERROR_INVALID_REQUEST = -32600,
    MCP_ERROR_METHOD_NOT_FOUND = -32601,
    MCP_ERROR_INVALID_PARAMS = -32602,
    MCP_ERROR_INTERNAL_ERROR = -32603
} mcp_error_code_t;

/**
 * MCP message type
 */
typedef enum {
    MCP_MESSAGE_TYPE_REQUEST,
    MCP_MESSAGE_TYPE_RESPONSE,
    MCP_MESSAGE_TYPE_NOTIFICATION,
    MCP_MESSAGE_TYPE_ERROR
} mcp_message_type_t;

/**
 * MCP request/response ID type
 */
typedef union {
    int64_t number;
    char *string;
} mcp_id_t;

/**
 * MCP error structure
 */
typedef struct {
    int code;
    char *message;
    char *data;  /* Optional additional data */
} mcp_error_t;

/**
 * MCP message structure (generic)
 */
typedef struct {
    char *jsonrpc;           /* Always "2.0" */
    mcp_message_type_t type;
    
    /* For requests and notifications */
    char *method;
    char *params;            /* JSON string */
    
    /* For requests and responses */
    mcp_id_t id;
    int id_is_string;        /* 1 if id is string, 0 if number */
    
    /* For responses */
    char *result;            /* JSON string */
    
    /* For errors */
    mcp_error_t *error;
} mcp_message_t;

/**
 * MCP server capabilities
 */
typedef struct {
    int supports_resources;
    int supports_tools;
    int supports_prompts;
    int supports_logging;
} mcp_capabilities_t;

/**
 * MCP server info
 */
typedef struct {
    char *name;
    char *version;
} mcp_server_info_t;

/**
 * MCP initialization result
 */
typedef struct {
    char *protocol_version;
    mcp_server_info_t server_info;
    mcp_capabilities_t capabilities;
} mcp_init_result_t;

#endif /* MCP_TYPES_H */
