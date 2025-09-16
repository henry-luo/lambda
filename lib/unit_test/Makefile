CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O0
INCLUDES = -Iinclude
LDFLAGS = 

# Source files
SRCDIR = src
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:.c=.o)

# Library target
LIBNAME = libunit_test.a
TARGET = $(LIBNAME)

# Example test
EXAMPLE_TEST = example_test
EXAMPLE_SRC = example_test.c

.PHONY: all clean test example

all: $(TARGET)

$(TARGET): $(OBJECTS)
	ar rcs $@ $^
	@echo "Built unit test library: $@"

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build and run example test
example: $(TARGET) $(EXAMPLE_TEST)
	@echo "Running example test..."
	./$(EXAMPLE_TEST)

$(EXAMPLE_TEST): $(EXAMPLE_SRC) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< -L. -lunit_test

# Test the framework itself
test: example

clean:
	rm -f $(OBJECTS) $(TARGET) $(EXAMPLE_TEST)
	@echo "Cleaned build artifacts"

# Install headers (for integration with Lambda build system)
install: $(TARGET)
	@echo "Unit test framework ready for integration"

# Show library info
info:
	@echo "Unit Test Framework"
	@echo "==================="
	@echo "Library: $(TARGET)"
	@echo "Sources: $(SOURCES)"
	@echo "Headers: include/criterion/criterion.h (Criterion compatibility)"
	@echo "Example: $(EXAMPLE_TEST)"
