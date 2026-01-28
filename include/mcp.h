/**
 * @file mcp.h
 * @brief MCP (Model Context Protocol) library main interface
 * 
 * A pure C implementation of the Model Context Protocol with minimal dependencies.
 * Provides JSON-RPC 2.0 message handling over stdio transport.
 */

#ifndef MCP_H
#define MCP_H

#include "mcp_types.h"
#include <stdio.h>

/**
 * Request handler callback function type
 * 
 * @param method The method name from the request
 * @param params JSON string of parameters (may be NULL)
 * @param user_data User-provided context data
 * @return JSON string result (caller must free), or NULL on error
 */
typedef char* (*mcp_request_handler_t)(const char *method, const char *params, void *user_data);

/**
 * Notification handler callback function type
 * 
 * @param method The method name from the notification
 * @param params JSON string of parameters (may be NULL)
 * @param user_data User-provided context data
 */
typedef void (*mcp_notification_handler_t)(const char *method, const char *params, void *user_data);

/**
 * MCP server context
 */
typedef struct mcp_server mcp_server_t;

/**
 * Create a new MCP server
 * 
 * @param name Server name
 * @param version Server version
 * @return Pointer to new server context, or NULL on error
 */
mcp_server_t* mcp_server_create(const char *name, const char *version);

/**
 * Destroy an MCP server and free resources
 * 
 * @param server Server context to destroy
 */
void mcp_server_destroy(mcp_server_t *server);

/**
 * Set server capabilities
 * 
 * @param server Server context
 * @param capabilities Capabilities to set
 */
void mcp_server_set_capabilities(mcp_server_t *server, mcp_capabilities_t capabilities);

/**
 * Set request handler callback
 * 
 * @param server Server context
 * @param handler Request handler callback
 * @param user_data User data to pass to handler
 */
void mcp_server_set_request_handler(mcp_server_t *server, mcp_request_handler_t handler, void *user_data);

/**
 * Set notification handler callback
 * 
 * @param server Server context
 * @param handler Notification handler callback
 * @param user_data User data to pass to handler
 */
void mcp_server_set_notification_handler(mcp_server_t *server, mcp_notification_handler_t handler, void *user_data);

/**
 * Run the server event loop (blocking)
 * Reads from stdin and writes to stdout
 * 
 * @param server Server context
 * @return 0 on success, -1 on error
 */
int mcp_server_run(mcp_server_t *server);

/**
 * Send a notification from server to client
 * 
 * @param server Server context
 * @param method Method name
 * @param params JSON string of parameters (may be NULL)
 * @return 0 on success, -1 on error
 */
int mcp_server_send_notification(mcp_server_t *server, const char *method, const char *params);

/**
 * Parse a JSON-RPC message from string
 * 
 * @param json JSON string to parse
 * @return Parsed message, or NULL on error (caller must free with mcp_message_free)
 */
mcp_message_t* mcp_message_parse(const char *json);

/**
 * Serialize a message to JSON string
 * 
 * @param message Message to serialize
 * @return JSON string (caller must free), or NULL on error
 */
char* mcp_message_serialize(const mcp_message_t *message);

/**
 * Free a message structure
 * 
 * @param message Message to free
 */
void mcp_message_free(mcp_message_t *message);

/**
 * Create an error response message
 * 
 * @param id Request ID
 * @param id_is_string Whether ID is a string (1) or number (0)
 * @param code Error code
 * @param message Error message
 * @return Error message structure (caller must free with mcp_message_free)
 */
mcp_message_t* mcp_create_error_response(mcp_id_t id, int id_is_string, int code, const char *message);

/**
 * Create a success response message
 * 
 * @param id Request ID
 * @param id_is_string Whether ID is a string (1) or number (0)
 * @param result JSON result string
 * @return Response message structure (caller must free with mcp_message_free)
 */
mcp_message_t* mcp_create_response(mcp_id_t id, int id_is_string, const char *result);

#endif /* MCP_H */
