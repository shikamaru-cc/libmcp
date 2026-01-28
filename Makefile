# Makefile for libmcp

CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -Werror -std=c99 -pedantic -Iinclude
LDFLAGS = 

# Directories
SRC_DIR = src
INC_DIR = include
EXAMPLES_DIR = examples
BUILD_DIR = build

# Source files
SOURCES = $(SRC_DIR)/mcp_json.c $(SRC_DIR)/mcp_message.c $(SRC_DIR)/mcp_server.c
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Library
LIBRARY = libmcp.a

# Examples
EXAMPLES = $(EXAMPLES_DIR)/echo_server $(EXAMPLES_DIR)/tool_server

.PHONY: all clean examples

all: $(LIBRARY)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(OBJECTS)
	$(AR) rcs $@ $^

examples: $(EXAMPLES)

$(EXAMPLES_DIR)/echo_server: $(EXAMPLES_DIR)/echo_server.c $(LIBRARY)
	$(CC) $(CFLAGS) $< -L. -lmcp -o $@

$(EXAMPLES_DIR)/tool_server: $(EXAMPLES_DIR)/tool_server.c $(LIBRARY)
	$(CC) $(CFLAGS) $< -L. -lmcp -o $@

clean:
	rm -rf $(BUILD_DIR) $(LIBRARY) $(EXAMPLES)

install: $(LIBRARY)
	install -d $(DESTDIR)/usr/local/lib
	install -m 644 $(LIBRARY) $(DESTDIR)/usr/local/lib/
	install -d $(DESTDIR)/usr/local/include
	install -m 644 $(INC_DIR)/*.h $(DESTDIR)/usr/local/include/
