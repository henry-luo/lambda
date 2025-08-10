# Makefile for Lambda Schema Validator Tests
# Author: Henry Luo

CC = clang
CXX = clang++
CFLAGS = -Wall -Wextra -std=c11 -g -O0 -DDEBUG
CXXFLAGS = -Wall -Wextra -std=c++17 -g -O0 -DDEBUG

# Directories
VALIDATOR_DIR = ../../../lambda/validator
LIB_DIR = ../../../lib
TEMP_DIR = .

# Include paths
INCLUDES = -I$(VALIDATOR_DIR) -I$(LIB_DIR) -I$(LIB_DIR)/mem-pool/include

# Source files
VALIDATOR_SOURCES = \
	$(VALIDATOR_DIR)/validator.cpp \
	$(VALIDATOR_DIR)/error_reporting.c \
	$(VALIDATOR_DIR)/schema_parser.c

LIB_SOURCES = \
	$(LIB_DIR)/strbuf.c \
	$(LIB_DIR)/hashmap.c \
	$(LIB_DIR)/strview.c

TEST_SOURCES = \
	test_validator_basic.c \
	test_validator_advanced.c

# Object files
VALIDATOR_OBJECTS = $(VALIDATOR_SOURCES:.c=.o)
VALIDATOR_OBJECTS := $(VALIDATOR_OBJECTS:.cpp=.o)
LIB_OBJECTS = $(LIB_SOURCES:.c=.o)
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)

# Test executables
TEST_BASIC = test_validator_basic
TEST_ADVANCED = test_validator_advanced

.PHONY: all clean test test-basic test-advanced

all: $(TEST_BASIC) $(TEST_ADVANCED)

$(TEST_BASIC): test_validator_basic.o $(VALIDATOR_OBJECTS) $(LIB_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(INCLUDES)

$(TEST_ADVANCED): test_validator_advanced.o validator_enhanced.o $(VALIDATOR_OBJECTS) $(LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(INCLUDES)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test: test-basic test-advanced

test-basic: $(TEST_BASIC)
	@echo "Running basic validator tests..."
	./$(TEST_BASIC)

test-advanced: $(TEST_ADVANCED)
	@echo "Running advanced validator tests..."
	./$(TEST_ADVANCED)

clean:
	rm -f *.o $(TEST_BASIC) $(TEST_ADVANCED)
	rm -f $(VALIDATOR_DIR)/*.o $(LIB_DIR)/*.o

# Helper targets
schemas: simple_schemas.ls complex_schemas.ls
	@echo "Schema files ready for testing"

test-data: test_document.md test_data_valid.json test_data_invalid.json
	@echo "Test data files ready"

help:
	@echo "Lambda Schema Validator Test Suite"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build all test executables"
	@echo "  test         - Run all tests"
	@echo "  test-basic   - Run basic validator tests"
	@echo "  test-advanced- Run advanced validator tests with error recovery"
	@echo "  clean        - Clean build artifacts"
	@echo "  schemas      - Verify schema files"
	@echo "  test-data    - Verify test data files"
	@echo "  help         - Show this help message"
