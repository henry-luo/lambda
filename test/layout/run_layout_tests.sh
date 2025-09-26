#!/bin/bash

# Radiant Layout Engine Test Runner
# This script helps run layout tests and capture browser references

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$SCRIPT_DIR/tools"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
RADIANT_EXE="$PROJECT_ROOT/radiant.exe"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🎯 Radiant Layout Engine Test Runner${NC}"
echo "============================================"

# Check if Radiant executable exists
if [ ! -f "$RADIANT_EXE" ]; then
    echo -e "${RED}❌ Error: Radiant executable not found: $RADIANT_EXE${NC}"
    echo -e "${YELLOW}Please build Radiant first with: make build-radiant${NC}"
    exit 1
fi

# Check if Node.js dependencies are installed
if [ ! -d "$TOOLS_DIR/node_modules" ]; then
    echo -e "${YELLOW}📦 Installing Node.js dependencies...${NC}"
    cd "$TOOLS_DIR"
    npm install
    if [ $? -ne 0 ]; then
        echo -e "${RED}❌ Failed to install dependencies${NC}"
        exit 1
    fi
fi

cd "$TOOLS_DIR"

# Parse command line arguments
GENERATE_REFS=false
VERBOSE=false
CATEGORY=""
TOLERANCE="2.0"

while [[ $# -gt 0 ]]; do
    case $1 in
        -g|--generate-references)
            GENERATE_REFS=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -c|--category)
            CATEGORY="$2"
            shift 2
            ;;
        -t|--tolerance)
            TOLERANCE="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  -g, --generate-references  Generate browser references if missing"
            echo "  -v, --verbose              Show detailed failure information"
            echo "  -c, --category <name>      Test specific category (basic|intermediate|medium|advanced)"
            echo "  -t, --tolerance <pixels>   Layout difference tolerance in pixels (default: 2.0)"
            echo "  -h, --help                 Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                         # Test all categories"
            echo "  $0 -c basic                # Test basic category only"
            echo "  $0 -g -v                   # Generate references and show details"
            echo "  $0 -t 1.0                  # Use 1px tolerance"
            exit 0
            ;;
        *)
            echo -e "${RED}❌ Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Build the command
CMD="node test_layout_auto.js --radiant-exe \"$RADIANT_EXE\" --tolerance $TOLERANCE"

if [ "$GENERATE_REFS" = true ]; then
    CMD="$CMD --generate-references"
fi

if [ "$VERBOSE" = true ]; then
    CMD="$CMD --verbose"
fi

if [ -n "$CATEGORY" ]; then
    CMD="$CMD --category $CATEGORY"
fi

echo -e "${BLUE}📂 Test Configuration:${NC}"
echo "  Radiant executable: $RADIANT_EXE"
echo "  Tolerance: ${TOLERANCE}px"
echo "  Generate references: $GENERATE_REFS"
echo "  Verbose output: $VERBOSE"
if [ -n "$CATEGORY" ]; then
    echo "  Category: $CATEGORY"
else
    echo "  Category: all"
fi
echo ""

# Run the tests
echo -e "${GREEN}🚀 Running layout tests...${NC}"
eval $CMD

EXIT_CODE=$?

if [ $EXIT_CODE -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✅ Layout tests completed successfully!${NC}"
    echo -e "${BLUE}📊 Results saved to: $PROJECT_ROOT/test_output/layout_test_results.json${NC}"
    echo -e "${BLUE}📁 Browser references saved to: $SCRIPT_DIR/reference/${NC}"
else
    echo ""
    echo -e "${RED}❌ Layout tests failed with exit code: $EXIT_CODE${NC}"
fi

exit $EXIT_CODE
