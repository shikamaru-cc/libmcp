CC = gcc
CFLAGS = -g -Wall -Wextra -O2

CXX = g++
CXXFLAGS = -std=c++17 -g -Wall -Wextra -O2

all: build/hello

build:
	mkdir -p build

build/cJSON.o: cJSON.c cJSON.h | build
	$(CC) -c $(CFLAGS) cJSON.c -o build/cJSON.o

build/libmcp.o: libmcp.c libmcp.h cJSON.h | build
	$(CC) -c $(CFLAGS) libmcp.c -o build/libmcp.o

build/hello: examples/hello.c build/libmcp.o build/cJSON.o | build
	$(CC) $(CFLAGS) -I. examples/hello.c build/libmcp.o build/cJSON.o -o build/hello

build/json_example: examples/hello.cpp build/cJSON.o src/json.o | build
	$(CXX) $(CXXFLAGS) -Iinclude -I. examples/hello.cpp src/json.o build/cJSON.o -o build/json_example

src/json.o: src/json.cpp include/json.h | build
	$(CXX) $(CXXFLAGS) -Iinclude -I. -c src/json.cpp -o src/json.o

clean:
	rm -rf build

.PHONY: all clean
