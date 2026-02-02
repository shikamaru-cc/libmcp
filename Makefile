CC = gcc
CFLAGS = -g -Wall -Wextra -O2
CURL_CFLAGS = $(shell curl-config --cflags)
CURL_LIBS = $(shell curl-config --libs)

all: build/hello build/redmine

hello: build/hello
redmine: build/redmine

build:
	mkdir -p build

build/stb.o: stb.c stb.h | build
	$(CC) -c $(CFLAGS) stb.c -o build/stb.o

build/cJSON.o: cJSON.c cJSON.h | build
	$(CC) -c $(CFLAGS) cJSON.c -o build/cJSON.o

build/libmcp.o: libmcp.c libmcp.h cJSON.h | build
	$(CC) -c $(CFLAGS) libmcp.c -o build/libmcp.o

build/sds.o: sds.c sds.h | build
	$(CC) -c $(CFLAGS) sds.c -o build/sds.o

build/hello: examples/hello.c build/libmcp.o build/cJSON.o | build
	$(CC) $(CFLAGS) -I. examples/hello.c build/libmcp.o build/cJSON.o -o build/hello

build/redmine: examples/redmine.c build/libmcp.o build/cJSON.o build/stb.o build/sds.o | build
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -I. examples/redmine.c build/libmcp.o build/cJSON.o build/stb.o build/sds.o $(CURL_LIBS) -lm -o build/redmine

clean:
	rm -rf build

.PHONY: all clean
