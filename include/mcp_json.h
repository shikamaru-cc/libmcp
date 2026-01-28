/**
 * @file mcp_json.h
 * @brief Minimal JSON utilities for MCP
 * 
 * Simple JSON handling focused on MCP protocol needs.
 */

#ifndef MCP_JSON_H
#define MCP_JSON_H

#include <stdint.h>

/**
 * Create a JSON string value (escapes and quotes the string)
 * 
 * @param str String to convert
 * @return JSON string representation (caller must free), or NULL on error
 */
char* mcp_json_string(const char *str);

/**
 * Create a JSON number value
 * 
 * @param num Number to convert
 * @return JSON number representation (caller must free), or NULL on error
 */
char* mcp_json_number(int64_t num);

/**
 * Create a JSON object with key-value pairs
 * Accepts NULL-terminated list of key-value pairs
 * 
 * @param ... NULL-terminated list of const char* key, const char* value pairs
 * @return JSON object string (caller must free), or NULL on error
 */
char* mcp_json_object(const char *key, ...);

/**
 * Extract a string value from JSON object by key
 * 
 * @param json JSON object string
 * @param key Key to extract
 * @return Extracted string (caller must free), or NULL if not found
 */
char* mcp_json_get_string(const char *json, const char *key);

/**
 * Extract an integer value from JSON object by key
 * 
 * @param json JSON object string
 * @param key Key to extract
 * @param value Pointer to store the extracted value
 * @return 0 on success, -1 on error or not found
 */
int mcp_json_get_int(const char *json, const char *key, int64_t *value);

/**
 * Check if a key exists in JSON object
 * 
 * @param json JSON object string
 * @param key Key to check
 * @return 1 if exists, 0 otherwise
 */
int mcp_json_has_key(const char *json, const char *key);

#endif /* MCP_JSON_H */
