/**
 * @file mcp_message.c
 * @brief MCP message parsing and serialization
 */

#include "mcp.h"
#include "mcp_json.h"
#include "mcp_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

mcp_message_t* mcp_message_parse(const char *json) {
    if (!json) return NULL;
    
    mcp_message_t *msg = calloc(1, sizeof(mcp_message_t));
    if (!msg) return NULL;
    
    /* Check jsonrpc version */
    msg->jsonrpc = mcp_json_get_string(json, "jsonrpc");
    if (!msg->jsonrpc || strcmp(msg->jsonrpc, "2.0") != 0) {
        mcp_message_free(msg);
        return NULL;
    }
    
    /* Check for method (request or notification) */
    msg->method = mcp_json_get_string(json, "method");
    
    /* Check for id (request or response) */
    int has_id = mcp_json_has_key(json, "id");
    if (has_id) {
        /* Try to get as number first */
        int64_t id_num;
        if (mcp_json_get_int(json, "id", &id_num) == 0) {
            msg->id.number = id_num;
            msg->id_is_string = 0;
        } else {
            /* Try as string */
            msg->id.string = mcp_json_get_string(json, "id");
            if (msg->id.string) {
                msg->id_is_string = 1;
            }
        }
    }
    
    /* Get params if present */
    if (mcp_json_has_key(json, "params")) {
        /* For simplicity, extract params as raw JSON substring */
        const char *params_start = strstr(json, "\"params\"");
        if (params_start) {
            const char *colon = strchr(params_start, ':');
            if (colon) {
                colon++;
                while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
                
                /* Find the end of params value (simplified - could be object or array) */
                int depth = 0;
                const char *end = colon;
                char start_char = *end;
                
                if (start_char == '{' || start_char == '[') {
                    char close_char = (start_char == '{') ? '}' : ']';
                    end++;
                    depth = 1;
                    
                    while (*end && depth > 0) {
                        if (*end == start_char) depth++;
                        else if (*end == close_char) depth--;
                        end++;
                    }
                    
                    size_t len = end - colon;
                    msg->params = malloc(len + 1);
                    if (msg->params) {
                        memcpy(msg->params, colon, len);
                        msg->params[len] = '\0';
                    }
                }
            }
        }
    }
    
    /* Get result if present */
    if (mcp_json_has_key(json, "result")) {
        const char *result_start = strstr(json, "\"result\"");
        if (result_start) {
            const char *colon = strchr(result_start, ':');
            if (colon) {
                colon++;
                while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
                
                /* Extract result value */
                const char *end = colon;
                if (*end == '{' || *end == '[') {
                    char start_char = *end;
                    char close_char = (start_char == '{') ? '}' : ']';
                    int depth = 1;
                    end++;
                    
                    while (*end && depth > 0) {
                        if (*end == start_char) depth++;
                        else if (*end == close_char) depth--;
                        end++;
                    }
                } else if (*end == '"') {
                    /* String value */
                    end++;
                    while (*end && !(*end == '"' && *(end-1) != '\\')) end++;
                    if (*end == '"') end++;
                } else {
                    /* Number, boolean, or null */
                    while (*end && *end != ',' && *end != '}' && *end != ']') end++;
                }
                
                size_t len = end - colon;
                msg->result = malloc(len + 1);
                if (msg->result) {
                    memcpy(msg->result, colon, len);
                    msg->result[len] = '\0';
                }
            }
        }
    }
    
    /* Check for error */
    if (mcp_json_has_key(json, "error")) {
        msg->error = calloc(1, sizeof(mcp_error_t));
        if (msg->error) {
            /* Extract error object */
            const char *error_start = strstr(json, "\"error\"");
            if (error_start) {
                const char *colon = strchr(error_start, ':');
                if (colon) {
                    colon++;
                    while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
                    
                    if (*colon == '{') {
                        /* Find end of error object */
                        const char *end = colon + 1;
                        int depth = 1;
                        while (*end && depth > 0) {
                            if (*end == '{') depth++;
                            else if (*end == '}') depth--;
                            end++;
                        }
                        
                        size_t len = end - colon;
                        char *error_obj = malloc(len + 1);
                        if (error_obj) {
                            memcpy(error_obj, colon, len);
                            error_obj[len] = '\0';
                            
                            /* Extract error fields */
                            int64_t code;
                            if (mcp_json_get_int(error_obj, "code", &code) == 0) {
                                msg->error->code = (int)code;
                            }
                            msg->error->message = mcp_json_get_string(error_obj, "message");
                            msg->error->data = mcp_json_get_string(error_obj, "data");
                            
                            free(error_obj);
                        }
                    }
                }
            }
            msg->type = MCP_MESSAGE_TYPE_ERROR;
        }
    }
    
    /* Determine message type */
    if (msg->type != MCP_MESSAGE_TYPE_ERROR) {
        if (msg->method) {
            if (has_id) {
                msg->type = MCP_MESSAGE_TYPE_REQUEST;
            } else {
                msg->type = MCP_MESSAGE_TYPE_NOTIFICATION;
            }
        } else if (has_id) {
            msg->type = MCP_MESSAGE_TYPE_RESPONSE;
        }
    }
    
    return msg;
}

char* mcp_message_serialize(const mcp_message_t *message) {
    if (!message) return NULL;
    
    char *jsonrpc = mcp_json_string(MCP_JSONRPC_VERSION);
    if (!jsonrpc) return NULL;
    
    char *id_str = NULL;
    if (message->type == MCP_MESSAGE_TYPE_REQUEST || 
        message->type == MCP_MESSAGE_TYPE_RESPONSE ||
        message->type == MCP_MESSAGE_TYPE_ERROR) {
        
        if (message->id_is_string) {
            id_str = mcp_json_string(message->id.string);
        } else {
            id_str = mcp_json_number(message->id.number);
        }
        if (!id_str) {
            free(jsonrpc);
            return NULL;
        }
    }
    
    char *result = NULL;
    
    if (message->type == MCP_MESSAGE_TYPE_REQUEST || 
        message->type == MCP_MESSAGE_TYPE_NOTIFICATION) {
        /* Request or notification */
        char *method = mcp_json_string(message->method);
        if (!method) {
            free(jsonrpc);
            free(id_str);
            return NULL;
        }
        
        if (message->type == MCP_MESSAGE_TYPE_REQUEST) {
            if (message->params) {
                result = mcp_json_object("jsonrpc", jsonrpc, 
                                        "id", id_str,
                                        "method", method,
                                        "params", message->params,
                                        NULL);
            } else {
                result = mcp_json_object("jsonrpc", jsonrpc,
                                        "id", id_str,
                                        "method", method,
                                        NULL);
            }
        } else {
            /* Notification */
            if (message->params) {
                result = mcp_json_object("jsonrpc", jsonrpc,
                                        "method", method,
                                        "params", message->params,
                                        NULL);
            } else {
                result = mcp_json_object("jsonrpc", jsonrpc,
                                        "method", method,
                                        NULL);
            }
        }
        
        free(method);
    } else if (message->type == MCP_MESSAGE_TYPE_RESPONSE) {
        /* Success response */
        const char *res = message->result ? message->result : "null";
        result = mcp_json_object("jsonrpc", jsonrpc,
                                "id", id_str,
                                "result", res,
                                NULL);
    } else if (message->type == MCP_MESSAGE_TYPE_ERROR) {
        /* Error response */
        if (message->error) {
            char *code = mcp_json_number(message->error->code);
            char *msg = mcp_json_string(message->error->message ? message->error->message : "Unknown error");
            
            char *error_obj;
            if (message->error->data) {
                char *data = mcp_json_string(message->error->data);
                error_obj = mcp_json_object("code", code,
                                           "message", msg,
                                           "data", data,
                                           NULL);
                free(data);
            } else {
                error_obj = mcp_json_object("code", code,
                                           "message", msg,
                                           NULL);
            }
            
            free(code);
            free(msg);
            
            if (error_obj) {
                result = mcp_json_object("jsonrpc", jsonrpc,
                                        "id", id_str,
                                        "error", error_obj,
                                        NULL);
                free(error_obj);
            }
        }
    }
    
    free(jsonrpc);
    free(id_str);
    
    return result;
}

void mcp_message_free(mcp_message_t *message) {
    if (!message) return;
    
    free(message->jsonrpc);
    free(message->method);
    free(message->params);
    free(message->result);
    
    if (message->id_is_string) {
        free(message->id.string);
    }
    
    if (message->error) {
        free(message->error->message);
        free(message->error->data);
        free(message->error);
    }
    
    free(message);
}

mcp_message_t* mcp_create_error_response(mcp_id_t id, int id_is_string, int code, const char *message) {
    mcp_message_t *msg = calloc(1, sizeof(mcp_message_t));
    if (!msg) return NULL;
    
    msg->jsonrpc = mcp_strdup(MCP_JSONRPC_VERSION);
    msg->type = MCP_MESSAGE_TYPE_ERROR;
    msg->id = id;
    msg->id_is_string = id_is_string;
    
    if (id_is_string && id.string) {
        msg->id.string = mcp_strdup(id.string);
    }
    
    msg->error = calloc(1, sizeof(mcp_error_t));
    if (!msg->error) {
        mcp_message_free(msg);
        return NULL;
    }
    
    msg->error->code = code;
    msg->error->message = mcp_strdup(message ? message : "Unknown error");
    
    return msg;
}

mcp_message_t* mcp_create_response(mcp_id_t id, int id_is_string, const char *result) {
    mcp_message_t *msg = calloc(1, sizeof(mcp_message_t));
    if (!msg) return NULL;
    
    msg->jsonrpc = mcp_strdup(MCP_JSONRPC_VERSION);
    msg->type = MCP_MESSAGE_TYPE_RESPONSE;
    msg->id = id;
    msg->id_is_string = id_is_string;
    
    if (id_is_string && id.string) {
        msg->id.string = mcp_strdup(id.string);
    }
    
    msg->result = mcp_strdup(result ? result : "null");
    
    return msg;
}
