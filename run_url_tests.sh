#!/bin/bash

# URL Parser Test Runner
# Compiles and runs the complete URL parser test suite

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "${BLUE}🔧 URL Parser Test Suite Compiler${NC}"
echo "========================================"

# Check if required files exist
if [ ! -f "test_url_complete.c" ]; then
    echo -e "${RED}❌ Error: test_url_complete.c not found${NC}"
    exit 1
fi

if [ ! -f "lib/url.c" ]; then
    echo -e "${RED}❌ Error: lib/url.c not found${NC}"
    exit 1
fi

# Compile the test
echo -e "${YELLOW}📦 Compiling URL parser test suite...${NC}"

COMPILE_CMD="clang -std=c99 -I. -Ilib/mem-pool/include \
    test_url_complete.c \
    lib/url.c \
    lib/url_parser.c \
    lib/url_compat.c \
    lib/string.c \
    lib/mem-pool/src/buffer.c \
    lib/mem-pool/src/utils.c \
    lib/mem-pool/src/variable.c \
    -o test_url_complete"

if ! $COMPILE_CMD; then
    echo -e "${RED}❌ Compilation failed${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Compilation successful${NC}"

# Run the test
echo ""
echo -e "${YELLOW}🚀 Running URL parser tests...${NC}"
echo "========================================"

if ./test_url_complete; then
    echo ""
    echo -e "${GREEN}🎉 All URL parser tests passed!${NC}"
    echo -e "${GREEN}✅ The modern C URL parser is ready to replace lexbor${NC}"
    
    # Clean up executable unless --keep flag is provided
    if [[ "$1" != "--keep" ]]; then
        rm -f test_url_complete
        echo -e "${BLUE}🧹 Cleaned up test executable${NC}"
    else
        echo -e "${BLUE}📁 Test executable kept as requested${NC}"
    fi
    
    exit 0
else
    echo ""
    echo -e "${RED}❌ Some tests failed${NC}"
    rm -f test_url_complete
    exit 1
fi
