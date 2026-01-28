/**
 * @file mcp_json.c
 * @brief Minimal JSON utilities implementation
 */

#include "mcp_json.h"
#include "mcp_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>

/* Helper to escape a string for JSON */
static char* escape_string(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    /* Worst case: every char needs \uXXXX (6 chars) */
    size_t escaped_len = len * 6 + 1;
    char *escaped = malloc(escaped_len);
    if (!escaped) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
            case '"':  escaped[j++] = '\\'; escaped[j++] = '"'; break;
            case '\\': escaped[j++] = '\\'; escaped[j++] = '\\'; break;
            case '\b': escaped[j++] = '\\'; escaped[j++] = 'b'; break;
            case '\f': escaped[j++] = '\\'; escaped[j++] = 'f'; break;
            case '\n': escaped[j++] = '\\'; escaped[j++] = 'n'; break;
            case '\r': escaped[j++] = '\\'; escaped[j++] = 'r'; break;
            case '\t': escaped[j++] = '\\'; escaped[j++] = 't'; break;
            default:
                if (str[i] < 32) {
                    /* Control characters */
                    j += snprintf(escaped + j, escaped_len - j, "\\u%04x", (unsigned char)str[i]);
                } else {
                    escaped[j++] = str[i];
                }
                break;
        }
    }
    escaped[j] = '\0';
    return escaped;
}

char* mcp_json_string(const char *str) {
    if (!str) {
        return mcp_strdup("null");
    }
    
    char *escaped = escape_string(str);
    if (!escaped) return NULL;
    
    size_t len = strlen(escaped) + 3; /* quotes + null */
    char *result = malloc(len);
    if (!result) {
        free(escaped);
        return NULL;
    }
    
    snprintf(result, len, "\"%s\"", escaped);
    free(escaped);
    return result;
}

char* mcp_json_number(int64_t num) {
    char *result = malloc(32);
    if (!result) return NULL;
    snprintf(result, 32, "%lld", (long long)num);
    return result;
}

char* mcp_json_object(const char *key, ...) {
    if (!key) {
        return mcp_strdup("{}");
    }
    
    va_list args;
    size_t capacity = 256;
    char *result = malloc(capacity);
    if (!result) return NULL;
    
    size_t pos = 0;
    result[pos++] = '{';
    
    va_start(args, key);
    int first = 1;
    
    while (key != NULL) {
        const char *value = va_arg(args, const char*);
        if (!value) {
            va_end(args);
            free(result);
            return NULL;
        }
        
        /* Add comma separator */
        if (!first) {
            if (pos + 1 >= capacity) {
                capacity *= 2;
                char *new_result = realloc(result, capacity);
                if (!new_result) {
                    free(result);
                    va_end(args);
                    return NULL;
                }
                result = new_result;
            }
            result[pos++] = ',';
        }
        first = 0;
        
        /* Add key */
        size_t key_len = strlen(key);
        if (pos + key_len + 4 >= capacity) {
            capacity = (capacity + key_len + 4) * 2;
            char *new_result = realloc(result, capacity);
            if (!new_result) {
                free(result);
                va_end(args);
                return NULL;
            }
            result = new_result;
        }
        result[pos++] = '"';
        memcpy(result + pos, key, key_len);
        pos += key_len;
        result[pos++] = '"';
        result[pos++] = ':';
        
        /* Add value */
        size_t value_len = strlen(value);
        if (pos + value_len + 1 >= capacity) {
            capacity = (capacity + value_len + 1) * 2;
            char *new_result = realloc(result, capacity);
            if (!new_result) {
                free(result);
                va_end(args);
                return NULL;
            }
            result = new_result;
        }
        memcpy(result + pos, value, value_len);
        pos += value_len;
        
        key = va_arg(args, const char*);
    }
    
    va_end(args);
    
    if (pos + 2 >= capacity) {
        char *new_result = realloc(result, pos + 2);
        if (!new_result) {
            free(result);
            return NULL;
        }
        result = new_result;
    }
    result[pos++] = '}';
    result[pos] = '\0';
    
    return result;
}

/* Simple JSON string extraction - finds quoted value after key */
char* mcp_json_get_string(const char *json, const char *key) {
    if (!json || !key) return NULL;
    
    /* Build search pattern: "key": */
    size_t pattern_len = strlen(key) + 10;
    char *pattern = malloc(pattern_len);
    if (!pattern) return NULL;
    snprintf(pattern, pattern_len, "\"%s\":", key);
    
    const char *key_pos = strstr(json, pattern);
    free(pattern);
    
    if (!key_pos) return NULL;
    
    /* Find the colon after key */
    const char *colon = strchr(key_pos, ':');
    if (!colon) return NULL;
    
    /* Skip whitespace */
    colon++;
    while (*colon && isspace(*colon)) colon++;
    
    /* Check if value is a string (starts with quote) */
    if (*colon != '"') return NULL;
    
    colon++; /* Skip opening quote */
    const char *end = colon;
    
    /* Find closing quote, handling escapes */
    int escaped = 0;
    while (*end) {
        if (escaped) {
            escaped = 0;
        } else if (*end == '\\') {
            escaped = 1;
        } else if (*end == '"') {
            break;
        }
        end++;
    }
    
    if (*end != '"') return NULL;
    
    size_t len = end - colon;
    char *result = malloc(len + 1);
    if (!result) return NULL;
    
    memcpy(result, colon, len);
    result[len] = '\0';
    
    return result;
}

int mcp_json_get_int(const char *json, const char *key, int64_t *value) {
    if (!json || !key || !value) return -1;
    
    /* Build search pattern: "key": */
    size_t pattern_len = strlen(key) + 10;
    char *pattern = malloc(pattern_len);
    if (!pattern) return -1;
    snprintf(pattern, pattern_len, "\"%s\":", key);
    
    const char *key_pos = strstr(json, pattern);
    free(pattern);
    
    if (!key_pos) return -1;
    
    /* Find the colon after key */
    const char *colon = strchr(key_pos, ':');
    if (!colon) return -1;
    
    /* Skip whitespace */
    colon++;
    while (*colon && isspace(*colon)) colon++;
    
    /* Parse number */
    char *endptr;
    *value = strtoll(colon, &endptr, 10);
    
    if (endptr == colon) return -1; /* No conversion */
    
    return 0;
}

int mcp_json_has_key(const char *json, const char *key) {
    if (!json || !key) return 0;
    
    size_t pattern_len = strlen(key) + 10;
    char *pattern = malloc(pattern_len);
    if (!pattern) return 0;
    snprintf(pattern, pattern_len, "\"%s\":", key);
    
    const char *found = strstr(json, pattern);
    free(pattern);
    
    return found != NULL;
}
