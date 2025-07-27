# Lambda Validator Test Suite Makefile

# Detect OS for library paths
UNAME_S := $(shell uname -s)

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O0
DEBUG_FLAGS = -DDEBUG -g3 -fsanitize=address -fsanitize=undefined

# Project paths
VALIDATOR_DIR = ..
LAMBDA_DIR = ../..
PARENT_DIR = ../../..

# Include paths
INCLUDES = -I$(VALIDATOR_DIR) -I$(LAMBDA_DIR) -I$(PARENT_DIR) -I$(LAMBDA_DIR)/tree-sitter/lib/include -I$(PARENT_DIR)/lib/mem-pool/include

# Criterion library paths (adjust based on your installation)
ifeq ($(UNAME_S),Darwin)
    # macOS with Homebrew
    CRITERION_PATH = /opt/homebrew
    CRITERION_LIBS = -L$(CRITERION_PATH)/lib -lcriterion
    CRITERION_INCLUDES = -I$(CRITERION_PATH)/include
else ifeq ($(UNAME_S),Linux)
    # Linux (Ubuntu/Debian)
    CRITERION_LIBS = -lcriterion
    CRITERION_INCLUDES = 
else
    # Default fallback
    CRITERION_LIBS = -lcriterion
    CRITERION_INCLUDES = 
endif

# Library dependencies
LIBS = $(CRITERION_LIBS) -lm

# All includes combined
ALL_INCLUDES = $(INCLUDES) $(CRITERION_INCLUDES)

# Source files
VALIDATOR_SOURCES = \
	$(VALIDATOR_DIR)/validator.c \
	$(VALIDATOR_DIR)/schema_parser.c \
	$(VALIDATOR_DIR)/doc_validators.c \
	$(VALIDATOR_DIR)/error_reporting.c

# Minimal validator sources (without doc_validators for basic tests)
VALIDATOR_CORE_SOURCES = \
	$(VALIDATOR_DIR)/validator.c \
	$(VALIDATOR_DIR)/schema_parser.c \
	$(VALIDATOR_DIR)/error_reporting.c

# Lambda library sources (essential ones needed for testing)
LAMBDA_SOURCES = \
	$(LAMBDA_DIR)/lambda.c \
	$(LAMBDA_DIR)/transpiler.c \
	$(LAMBDA_DIR)/build_ast.c \
	$(LAMBDA_DIR)/parse.c \
	$(PARENT_DIR)/utils.c \
	$(PARENT_DIR)/variable.c \
	$(PARENT_DIR)/arraylist.c \
	$(PARENT_DIR)/hashmap.c \
	$(PARENT_DIR)/strbuf.c \
	$(PARENT_DIR)/strview.c \
	$(PARENT_DIR)/utf.c

# Test source files
TEST_SOURCES = \
	test_validator.c \
	test_schema_parser.c \
	test_doc_schema_simple.c \
	test_api.c \
	test_simple.c

# Object files
VALIDATOR_OBJECTS = $(VALIDATOR_SOURCES:.c=.o)
VALIDATOR_CORE_OBJECTS = $(VALIDATOR_CORE_SOURCES:.c=.o)
LAMBDA_OBJECTS = $(LAMBDA_SOURCES:.c=.o)
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)

# Test executables
TEST_EXECUTABLES = \
	test_validator \
	test_schema_parser \
	test_doc_schema_simple \
	test_api \
	test_simple \
	test_all

# Default target
.PHONY: all
all: test_all run

# Build all tests as separate executables
.PHONY: tests
tests: $(TEST_EXECUTABLES)

# Individual test executables
test_validator: test_validator.o $(VALIDATOR_OBJECTS) $(LAMBDA_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test_schema_parser: test_schema_parser.o $(VALIDATOR_CORE_OBJECTS) $(LAMBDA_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test_doc_schema_simple: test_doc_schema_simple.o $(VALIDATOR_OBJECTS) $(LAMBDA_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test_api: test_api.o $(VALIDATOR_OBJECTS) $(LAMBDA_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test_simple: test_simple.o $(VALIDATOR_OBJECTS) $(LAMBDA_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Combined test executable
test_all: $(TEST_OBJECTS) $(VALIDATOR_OBJECTS) $(LAMBDA_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Compile test object files
%.o: %.c
	$(CC) $(CFLAGS) $(ALL_INCLUDES) -c $< -o $@

# Compile validator object files  
$(VALIDATOR_DIR)/%.o: $(VALIDATOR_DIR)/%.c
	$(CC) $(CFLAGS) $(ALL_INCLUDES) -c $< -o $@

# Compile lambda object files
$(LAMBDA_DIR)/%.o: $(LAMBDA_DIR)/%.c
	$(CC) $(CFLAGS) $(ALL_INCLUDES) -c $< -o $@

$(PARENT_DIR)/%.o: $(PARENT_DIR)/%.c
	$(CC) $(CFLAGS) $(ALL_INCLUDES) -c $< -o $@

# Running tests
.PHONY: run
run: test_all
	@echo "Running validator test suite..."
	./test_all

.PHONY: run-individual
run-individual: tests
	@echo "Running individual test suites..."
	@echo "=== Basic Validator Tests ==="
	./test_validator
	@echo ""
	@echo "=== Schema Parser Tests ==="
	./test_schema_parser
	@echo ""
	@echo "=== Doc Schema Tests ==="
	./test_doc_schema
	@echo ""
	@echo "=== API Tests ==="
	./test_api

# Debug builds
.PHONY: debug
debug: CFLAGS += $(DEBUG_FLAGS)
debug: test_all

.PHONY: debug-run
debug-run: debug
	@echo "Running tests with debug flags..."
	./test_all

# Memory testing with Valgrind (Linux/macOS)
.PHONY: memcheck
memcheck: test_all
	@echo "Running memory check with Valgrind..."
	valgrind --tool=memcheck --leak-check=full --show-reachable=yes \
		--track-origins=yes --error-exitcode=1 ./test_all

# Test with specific suites
.PHONY: test-validator
test-validator: test_validator
	./test_validator

.PHONY: test-parser
test-parser: test_schema_parser
	./test_schema_parser

.PHONY: test-doc
test-doc: test_doc_schema
	./test_doc_schema

.PHONY: test-api
test-api: test_api
	./test_api

# Verbose test output
.PHONY: verbose
verbose: test_all
	./test_all --verbose

# Test with specific patterns
.PHONY: test-pattern
test-pattern: test_all
	@if [ -z "$(PATTERN)" ]; then \
		echo "Usage: make test-pattern PATTERN=<test_pattern>"; \
		echo "Example: make test-pattern PATTERN='*primitive*'"; \
	else \
		./test_all --pattern="$(PATTERN)"; \
	fi

# Performance/benchmark tests
.PHONY: benchmark
benchmark: CFLAGS += -O3 -DNDEBUG
benchmark: test_all
	@echo "Running performance benchmarks..."
	./test_all --tap

# Clean targets
.PHONY: clean
clean:
	rm -f *.o $(TEST_EXECUTABLES)
	rm -f $(VALIDATOR_OBJECTS) $(LAMBDA_OBJECTS)
	rm -f *.gcov *.gcda *.gcno

.PHONY: clean-all
clean-all: clean
	rm -rf *.dSYM/
	rm -f core.*

# Check dependencies
.PHONY: check-deps
check-deps:
	@echo "Checking test dependencies..."
	@echo "Criterion library:"
	@$(CC) $(CRITERION_INCLUDES) -lcriterion -x c -c /dev/null -o /dev/null 2>/dev/null && \
		echo "  ✓ Criterion found" || echo "  ✗ Criterion not found"
	@echo "Lambda headers:"
	@test -f $(LAMBDA_DIR)/lambda.h && echo "  ✓ lambda.h found" || echo "  ✗ lambda.h not found"
	@test -f $(LAMBDA_DIR)/transpiler.h && echo "  ✓ transpiler.h found" || echo "  ✗ transpiler.h not found"
	@echo "Validator headers:"
	@test -f $(VALIDATOR_DIR)/validator.h && echo "  ✓ validator.h found" || echo "  ✗ validator.h not found"

# Install criterion (macOS with Homebrew)
.PHONY: install-criterion-mac
install-criterion-mac:
	brew install criterion

# Install criterion (Ubuntu/Debian)
.PHONY: install-criterion-linux
install-criterion-linux:
	sudo apt-get update
	sudo apt-get install libcriterion-dev

# Test coverage (requires gcov)
.PHONY: coverage
coverage: CFLAGS += --coverage
coverage: test_all
	./test_all
	gcov *.gcda
	@echo "Coverage files generated: *.gcov"

# Help target
.PHONY: help
help:
	@echo "Lambda Validator Test Suite"
	@echo ""
	@echo "Targets:"
	@echo "  all              Build and run all tests (default)"
	@echo "  tests            Build all test executables"
	@echo "  run              Run combined test suite"
	@echo "  run-individual   Run each test suite separately"
	@echo "  debug            Build with debug flags"
	@echo "  debug-run        Build and run with debug flags"
	@echo "  memcheck         Run tests with Valgrind memory checking"
	@echo "  verbose          Run tests with verbose output"
	@echo "  benchmark        Run performance benchmarks"
	@echo "  coverage         Generate test coverage report"
	@echo ""
	@echo "Individual test targets:"
	@echo "  test-validator   Run core validator tests"
	@echo "  test-parser      Run schema parser tests"
	@echo "  test-doc         Run doc schema tests"
	@echo "  test-api         Run public API tests"
	@echo ""
	@echo "Utility targets:"
	@echo "  check-deps       Check for required dependencies"
	@echo "  clean            Remove build artifacts"
	@echo "  clean-all        Remove all generated files"
	@echo "  help             Show this help message"
	@echo ""
	@echo "Pattern matching:"
	@echo "  make test-pattern PATTERN='*primitive*'"
	@echo ""
	@echo "Dependencies:"
	@echo "  - Criterion testing framework"
	@echo "  - Lambda transpiler library"
	@echo "  - GCC or compatible C compiler"

# Ensure object directories exist
$(VALIDATOR_OBJECTS) $(LAMBDA_OBJECTS): | check-dirs

.PHONY: check-dirs
check-dirs:
	@test -d $(VALIDATOR_DIR) || (echo "Error: Validator directory not found" && exit 1)
	@test -d $(LAMBDA_DIR) || (echo "Error: Lambda directory not found" && exit 1)

# Automatic dependency generation
-include $(TEST_OBJECTS:.o=.d)
-include $(VALIDATOR_OBJECTS:.o=.d)
-include $(LAMBDA_OBJECTS:.o=.d)

%.d: %.c
	@$(CC) $(ALL_INCLUDES) -MM $< | sed 's|\($*\)\.o[ :]*|\1.o $@ : |g' > $@

.PHONY: deps
deps: $(TEST_OBJECTS:.o=.d) $(VALIDATOR_OBJECTS:.o=.d) $(LAMBDA_OBJECTS:.o=.d)

# Mark all test targets as phony
.PHONY: $(TEST_EXECUTABLES)
