# Lambda Schema Validator Makefile
# Integrates with existing Lambda build system

# Get parent directory settings
PARENT_DIR = ..
include $(PARENT_DIR)/Makefile.common

# Validator specific settings
VALIDATOR_DIR = $(PARENT_DIR)/validator
VALIDATOR_SOURCES = $(wildcard $(VALIDATOR_DIR)/*.c)
VALIDATOR_HEADERS = $(wildcard $(VALIDATOR_DIR)/*.h)
VALIDATOR_OBJECTS = $(VALIDATOR_SOURCES:.c=.o)

# Main validator sources (excluding main.c for library)
VALIDATOR_LIB_SOURCES = validator.cpp validation_engine.c \
                       doc_validators.c error_reporting.c validate.cpp
VALIDATOR_LIB_OBJECTS = $(VALIDATOR_LIB_SOURCES:.cpp=.o)
VALIDATOR_LIB_OBJECTS := $(VALIDATOR_LIB_OBJECTS:.c=.o)

# Dependencies from parent Lambda implementation (AST-based)
LAMBDA_LIB = $(PARENT_DIR)/lambda.a
REQUIRED_LIBS = $(LAMBDA_LIB) \
               $(PARENT_DIR)/lib/arraylist.o \
               $(PARENT_DIR)/lib/hashmap.o \
               $(PARENT_DIR)/lib/strbuf.o \
               $(PARENT_DIR)/lib/strview.o \
               $(PARENT_DIR)/lib/mem-pool/src/variable.o \
               $(PARENT_DIR)/lib/mem-pool/src/buffer.o \
               $(PARENT_DIR)/transpiler.o \
               $(PARENT_DIR)/build_ast.o

# External dependencies
TREE_SITTER_LIB = $(PARENT_DIR)/lambda/tree-sitter/libtree-sitter.a
TREE_SITTER_LAMBDA = $(PARENT_DIR)/lambda/tree-sitter-lambda/src/parser.c

# Compiler flags
CC = gcc
CXX = g++
CFLAGS = -std=c11 -Wall -Wextra -O2 -g
CXXFLAGS = -std=c++23 -Wall -Wextra -O2 -g
INCLUDES = -I$(PARENT_DIR) -I$(PARENT_DIR)/lib -I$(PARENT_DIR)/lib/mem-pool/include \
           -I$(PARENT_DIR)/lambda -I$(PARENT_DIR)/lambda/tree-sitter/lib/include \
           -I$(PARENT_DIR)/lambda/validator
DEFINES = -DLAMBDA_VALIDATOR -DLAMBDA_AST_VALIDATOR

# Debug build settings
DEBUG_CFLAGS = -DDEBUG -O0 -g3 -fsanitize=address -fsanitize=undefined
RELEASE_CFLAGS = -DNDEBUG -O3 -flto

# Default target
all: libvalidator.a lambda-validate

# Build modes
debug: CFLAGS += $(DEBUG_CFLAGS)
debug: all

release: CFLAGS += $(RELEASE_CFLAGS)
release: all

# Library target
libvalidator.a: $(VALIDATOR_LIB_OBJECTS)
	@echo "Creating validator library..."
	ar rcs $@ $^
	@echo "Validator library created: $@"

# CLI tool target
lambda-validate: main.o libvalidator.a $(REQUIRED_LIBS)
	@echo "Building validator CLI tool..."
	$(CXX) $(CXXFLAGS) -o $@ main.o libvalidator.a $(REQUIRED_LIBS) $(TREE_SITTER_LIB) -lm
	@echo "CLI tool created: $@"

# Object file compilation
%.o: %.c $(VALIDATOR_HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

# C++ Object file compilation
%.o: %.cpp $(VALIDATOR_HEADERS)
	@echo "Compiling C++ $<..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

# Dependency on parent Lambda library
$(LAMBDA_LIB):
	@echo "Building required Lambda library..."
	$(MAKE) -C $(PARENT_DIR) lambda.a

# Tree-sitter dependency
$(TREE_SITTER_LIB):
	@echo "Building Tree-sitter library..."
	$(MAKE) -C $(PARENT_DIR)/lambda/tree-sitter

# Documentation
docs: $(VALIDATOR_HEADERS) $(VALIDATOR_SOURCES)
	@echo "Generating documentation..."
	doxygen Doxyfile

# Testing
test: lambda-validate
	@echo "Running validator tests..."
	./run_tests.sh

# Test with sample files
test-samples: lambda-validate
	@echo "Testing with sample files..."
	./lambda-validate -s ../input/doc_schema.ls -d ../input/sample.mark -v -w

# Performance benchmark
benchmark: lambda-validate
	@echo "Running performance benchmarks..."
	time ./lambda-validate -s ../input/doc_schema.ls -d ../input/large_document.mark

# Memory check
memcheck: lambda-validate
	@echo "Running memory check..."
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
		./lambda-validate -s ../input/doc_schema.ls -d ../input/sample.mark

# Install targets
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include

install: lambda-validate libvalidator.a
	@echo "Installing validator..."
	install -d $(BINDIR) $(LIBDIR) $(INCDIR)
	install -m 755 lambda-validate $(BINDIR)
	install -m 644 libvalidator.a $(LIBDIR)
	install -m 644 validator.h $(INCDIR)

uninstall:
	@echo "Uninstalling validator..."
	rm -f $(BINDIR)/lambda-validate
	rm -f $(LIBDIR)/libvalidator.a
	rm -f $(INCDIR)/validator.h

# Clean targets
clean:
	@echo "Cleaning validator build files..."
	rm -f *.o libvalidator.a lambda-validate
	rm -rf docs/

clean-all: clean
	@echo "Cleaning all build files including dependencies..."
	$(MAKE) -C $(PARENT_DIR) clean

# Development helpers
format:
	@echo "Formatting source code..."
	clang-format -i $(VALIDATOR_SOURCES) $(VALIDATOR_HEADERS)

lint:
	@echo "Running static analysis..."
	clang-tidy $(VALIDATOR_SOURCES) -- $(INCLUDES) $(DEFINES)

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@which gcc > /dev/null || (echo "gcc not found" && exit 1)
	@which ar > /dev/null || (echo "ar not found" && exit 1)
	@test -f $(PARENT_DIR)/lambda.h || (echo "Lambda headers not found" && exit 1)
	@echo "All dependencies satisfied"

# Help target
help:
	@echo "Lambda Schema Validator Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all            Build library and CLI tool (default)"
	@echo "  debug          Build with debug flags"
	@echo "  release        Build with release optimization"
	@echo "  libvalidator.a Build validator library only"
	@echo "  lambda-validate Build CLI tool only"
	@echo "  docs           Generate documentation"
	@echo "  test           Run test suite"
	@echo "  test-samples   Test with sample files"
	@echo "  benchmark      Run performance benchmarks"
	@echo "  memcheck       Run memory leak check"
	@echo "  install        Install to system"
	@echo "  uninstall      Remove from system"
	@echo "  clean          Clean build files"
	@echo "  clean-all      Clean all including dependencies"
	@echo "  format         Format source code"
	@echo "  lint           Run static analysis"
	@echo "  check-deps     Check build dependencies"
	@echo "  help           Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  CC             Compiler (default: gcc)"
	@echo "  CFLAGS         Compiler flags"
	@echo "  PREFIX         Install prefix (default: /usr/local)"

# Phony targets
.PHONY: all debug release docs test test-samples benchmark memcheck \
        install uninstall clean clean-all format lint check-deps help

# Dependency tracking
-include $(VALIDATOR_OBJECTS:.o=.d)

# Auto-generate dependencies
%.d: %.c
	@$(CC) $(INCLUDES) $(DEFINES) -MM $< > $@

# Example usage for development
example: lambda-validate
	@echo "Running example validation..."
	@echo "// Example schema" > example_schema.ls
	@echo "type SimpleDoc < title: string, content: string >" >> example_schema.ls
	@echo "<SimpleDoc title:\"Test\" \"Hello World\">" > example_doc.mark
	./lambda-validate -s example_schema.ls -d example_doc.mark -v
	@rm -f example_schema.ls example_doc.mark
