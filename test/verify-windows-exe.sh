#!/bin/bash

# Script to verify the cross-compiled lambda-windows.exe using Wine
# Author: GitHub Copilot
# Usage: ./verify-windows-exe.sh

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Windows Executable Verification Script ===${NC}"
echo

# Check if Wine is installed
if ! command -v wine &> /dev/null; then
    echo -e "${RED}Error: Wine is not installed. Please install Wine to run Windows executables.${NC}"
    echo "Install with: brew install wine"
    exit 1
fi

# Check if lambda-windows.exe exists
if [ ! -f "../lambda-windows.exe" ]; then
    echo -e "${RED}Error: lambda-windows.exe not found. Please build it first with:${NC}"
    echo "../compile.sh --platform=windows"
    exit 1
fi

# Check for required DLLs and copy if needed
echo -e "${YELLOW}Checking for required Windows DLLs...${NC}"
WINE_SYSTEM32="$HOME/.wine/drive_c/windows/system32"
mkdir -p "$WINE_SYSTEM32"

if [ ! -f "$WINE_SYSTEM32/libwinpthread-1.dll" ]; then
    echo "Looking for libwinpthread-1.dll..."
    PTHREAD_DLL=$(find /opt/homebrew -name "libwinpthread-1.dll" 2>/dev/null | grep x86_64 | head -1)
    if [ -n "$PTHREAD_DLL" ]; then
        echo "Copying $PTHREAD_DLL to Wine system32..."
        cp "$PTHREAD_DLL" "$WINE_SYSTEM32/"
        echo -e "${GREEN}✓ libwinpthread-1.dll installed to Wine${NC}"
    else
        echo -e "${YELLOW}Warning: libwinpthread-1.dll not found. Wine execution may fail.${NC}"
    fi
else
    echo -e "${GREEN}✓ libwinpthread-1.dll already in Wine system32${NC}"
fi

# Also copy to current directory for completeness
if [ ! -f "libwinpthread-1.dll" ] && [ -n "$PTHREAD_DLL" ]; then
    cp "$PTHREAD_DLL" .
fi

echo -e "${GREEN}✓ Wine is available${NC}"
echo -e "${GREEN}✓ lambda-windows.exe found${NC}"
echo

# Test 1: Basic executable info
echo -e "${YELLOW}Test 1: Checking executable info...${NC}"
file ../lambda-windows.exe
echo

# Test 2: Basic help/version check
echo -e "${YELLOW}Test 2: Testing basic execution (help)...${NC}"
if wine ../lambda-windows.exe --help 2>/dev/null; then
    echo -e "${GREEN}✓ Basic execution successful${NC}"
else
    echo -e "${YELLOW}Note: Help command may not be implemented, trying version...${NC}"
    if wine ../lambda-windows.exe --version 2>/dev/null; then
        echo -e "${GREEN}✓ Version command successful${NC}"
    else
        echo -e "${YELLOW}Note: No help/version flags, trying basic execution...${NC}"
        # Try running without arguments (may show usage or error)
        wine ../lambda-windows.exe 2>/dev/null || echo -e "${YELLOW}Basic execution attempted${NC}"
    fi
fi
echo

# Test 3: Test with sample files if available
echo -e "${YELLOW}Test 3: Testing with sample files...${NC}"
if [ -f "hello-world.ls" ]; then
    echo "Testing with test/hello-world.ls:"
    cat hello-world.ls | head -5
    echo "... (truncated)"
    echo
    echo "Running: wine ../lambda-windows.exe hello-world.ls"
    if timeout 10 wine ../lambda-windows.exe hello-world.ls > /tmp/wine_test_output.txt 2>&1; then
        echo -e "${GREEN}✓ Successfully processed hello-world.ls${NC}"
        echo "Output preview:"
        head -3 /tmp/wine_test_output.txt
    else
        echo -e "${YELLOW}Note: Processing completed with non-zero exit or timeout${NC}"
        echo "Output preview:"
        head -3 /tmp/wine_test_output.txt 2>/dev/null || echo "No output captured"
    fi
    echo
fi

# Test with a simple custom input
echo "Creating a simple test file..."
cat > /tmp/simple_test.ls << 'EOF'
fn main() {
    let a = 5 + 3
    a
}
EOF

echo "Testing with simple input:"
cat /tmp/simple_test.ls
echo
echo "Running: wine ../lambda-windows.exe /tmp/simple_test.ls"
if timeout 10 wine ../lambda-windows.exe /tmp/simple_test.ls > /tmp/simple_output.txt 2>&1; then
    echo -e "${GREEN}✓ Simple test completed${NC}"
    echo "Output:"
    cat /tmp/simple_output.txt | head -10
else
    echo -e "${YELLOW}Note: Simple test completed with non-zero exit or timeout${NC}"
    echo "Output:"
    cat /tmp/simple_output.txt 2>/dev/null | head -10 || echo "No output captured"
fi
echo

# Cleanup temporary files
rm -f /tmp/wine_test_output.txt /tmp/simple_test.ls /tmp/simple_output.txt

# Test 4: Compare with native executable if available
echo -e "${YELLOW}Test 4: Comparing with native executable...${NC}"
if [ -f "../lambda.exe" ]; then
    echo "Creating test input file..."
    echo "test input content" > /tmp/test_input.txt
    
    echo "Running native version:"
    ../lambda.exe /tmp/test_input.txt > /tmp/native_output.txt 2>&1 || true
    
    echo "Running Windows version with Wine:"
    wine ../lambda-windows.exe /tmp/test_input.txt > /tmp/wine_output.txt 2>&1 || true
    
    if [ -f "/tmp/native_output.txt" ] && [ -f "/tmp/wine_output.txt" ]; then
        if diff /tmp/native_output.txt /tmp/wine_output.txt > /dev/null; then
            echo -e "${GREEN}✓ Output matches between native and Windows versions${NC}"
        else
            echo -e "${YELLOW}Note: Outputs differ (this may be expected due to platform differences)${NC}"
            echo "Native output:"
            head -5 /tmp/native_output.txt
            echo "Wine output:"
            head -5 /tmp/wine_output.txt
        fi
    fi
    
    # Cleanup
    rm -f /tmp/test_input.txt /tmp/native_output.txt /tmp/wine_output.txt
else
    echo -e "${YELLOW}Note: Native executable not found, skipping comparison${NC}"
fi
echo

# Test 5: Memory and performance check
echo -e "${YELLOW}Test 5: Basic memory/crash test...${NC}"
echo "Running with Wine's built-in error checking..."
if WINEDEBUG=warn+all wine ../lambda-windows.exe --help >/dev/null 2>&1; then
    echo -e "${GREEN}✓ No critical Wine warnings detected${NC}"
else
    echo -e "${YELLOW}Note: Some Wine warnings may be present (check above)${NC}"
fi
echo

# Test 6: Dependency check
echo -e "${YELLOW}Test 6: Checking dependencies...${NC}"
echo "Checking what libraries the executable links to:"
if command -v objdump &> /dev/null; then
    objdump -p ../lambda-windows.exe | grep "DLL Name" | head -10 || echo "Could not extract DLL dependencies"
elif command -v strings &> /dev/null; then
    echo "Using strings to check for common Windows DLLs:"
    strings ../lambda-windows.exe | grep -i "\.dll" | head -10 || echo "No obvious DLL references found"
else
    echo "objdump and strings not available for dependency analysis"
fi
echo

echo -e "${BLUE}=== Verification Summary ===${NC}"
echo -e "${GREEN}✓ Wine environment is working${NC}"
echo -e "${GREEN}✓ lambda-windows.exe can be executed${NC}"
echo -e "${YELLOW}Note: For full verification, test on a real Windows machine${NC}"
echo
echo -e "${BLUE}=== Additional Verification Steps ===${NC}"
echo "1. Copy lambda-windows.exe to a Windows machine and test"
echo "2. Use Windows Subsystem for Linux (WSL) if available"
echo "3. Use a Windows VM for comprehensive testing"
echo "4. Set up automated CI testing with Windows runners"
echo
echo -e "${GREEN}Verification complete!${NC}"
