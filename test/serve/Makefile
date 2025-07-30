# Makefile for HTTP/HTTPS server library and tests

# project configuration
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2
INCLUDES = -I/opt/homebrew/include
LIBS = -L/opt/homebrew/lib -levent -levent_openssl -lssl -lcrypto

# source directories
LIB_SERVE_DIR = ../../lib/serve
TEST_DIR = .

# source files
LIB_SOURCES = $(LIB_SERVE_DIR)/utils.c \
              $(LIB_SERVE_DIR)/http_handler.c \
              $(LIB_SERVE_DIR)/tls_handler.c \
              $(LIB_SERVE_DIR)/server.c

TEST_SOURCES = $(TEST_DIR)/test_server.c
EXAMPLE_SOURCES = $(TEST_DIR)/example_server.c

# object files
LIB_OBJECTS = $(LIB_SOURCES:.c=.o)
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)
EXAMPLE_OBJECTS = $(EXAMPLE_SOURCES:.c=.o)

# targets
TARGETS = test_server example_server
LIB_TARGET = libserve.a

# default target
all: $(TARGETS)

# library target
$(LIB_TARGET): $(LIB_OBJECTS)
	ar rcs $@ $^

# test executable
test_server: $(TEST_OBJECTS) $(LIB_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# example executable
example_server: $(EXAMPLE_OBJECTS) $(LIB_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# compile source files
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# run tests
test: test_server
	./test_server

# run example (in background for quick test)
example: example_server
	@echo "starting example server on port 18080..."
	@./example_server -p 18080 -s 0 &
	@sleep 1
	@echo "testing with curl..."
	@curl -s http://localhost:18080/ || echo "curl test failed"
	@echo "stopping server..."
	@pkill -f "./example_server" || true

# install dependencies (macos with homebrew)
install-deps-mac:
	brew install libevent openssl

# install dependencies (ubuntu/debian)
install-deps-ubuntu:
	sudo apt-get update
	sudo apt-get install libevent-dev libssl-dev

# check for required libraries
check-deps:
	@echo "checking for required libraries..."
	@pkg-config --exists libevent || echo "libevent not found"
	@pkg-config --exists openssl || echo "openssl not found"
	@echo "dependencies check complete"

# clean build artifacts
clean:
	rm -f $(LIB_OBJECTS) $(TEST_OBJECTS) $(EXAMPLE_OBJECTS)
	rm -f $(TARGETS) $(LIB_TARGET)
	rm -f /tmp/test_cert.pem /tmp/test_key.pem
	rm -f /tmp/server_cert.pem /tmp/server_key.pem

# show help
help:
	@echo "available targets:"
	@echo "  all           - build all targets"
	@echo "  test_server   - build test executable"
	@echo "  example_server - build example server"
	@echo "  libserve.a    - build static library"
	@echo "  test          - run tests"
	@echo "  example       - run example server"
	@echo "  check-deps    - check for dependencies"
	@echo "  install-deps-mac - install dependencies on macos"
	@echo "  install-deps-ubuntu - install dependencies on ubuntu"
	@echo "  clean         - clean build artifacts"
	@echo "  help          - show this help"

.PHONY: all test example check-deps install-deps-mac install-deps-ubuntu clean help
