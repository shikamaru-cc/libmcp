/**
 * @file mcp_internal.h
 * @brief Internal utilities for MCP library
 */

#ifndef MCP_INTERNAL_H
#define MCP_INTERNAL_H

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

/* Define ssize_t for platforms that don't have it */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#ifdef _WIN32
typedef intptr_t ssize_t;
#else
#include <sys/types.h>
#endif
#endif

/* strdup implementation for C99 compatibility */
static inline char* mcp_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}

/* getline implementation for C99 compatibility */
static inline ssize_t mcp_getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) {
        return -1;
    }
    
    if (*lineptr == NULL || *n == 0) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) return -1;
    }
    
    size_t pos = 0;
    int c;
    
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            size_t new_size = *n * 2;
            char *new_ptr = realloc(*lineptr, new_size);
            if (!new_ptr) return -1;
            *lineptr = new_ptr;
            *n = new_size;
        }
        
        (*lineptr)[pos++] = (char)c;
        
        if (c == '\n') {
            break;
        }
    }
    
    if (pos == 0 && c == EOF) {
        return -1;
    }
    
    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

#endif /* MCP_INTERNAL_H */
