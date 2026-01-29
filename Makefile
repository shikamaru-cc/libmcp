CC = gcc
CFLAGS = -g -Wall -Wextra -O2

all: build/hello

build:
	mkdir -p build

build/cJSON.o: cJSON.c cJSON.h | build
	$(CC) -c $(CFLAGS) cJSON.c -o build/cJSON.o

build/libmcp.o: libmcp.c libmcp.h cJSON.h | build
	$(CC) -c $(CFLAGS) libmcp.c -o build/libmcp.o

build/sds.o: sds.c sds.h | build
	$(CC) -c $(CFLAGS) sds.c -o build/sds.o

build/hello: examples/hello.c build/libmcp.o build/cJSON.o build/sds.o | build
	$(CC) $(CFLAGS) -I. examples/hello.c build/libmcp.o build/cJSON.o build/sds.o -o build/hello

clean:
	rm -rf build

.PHONY: all clean
