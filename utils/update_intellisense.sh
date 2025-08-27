#!/bin/bash

# Script to update IntelliSense database for VS Code
# This regenerates compile_commands.json for better C++ intellisense

# Get the script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "🔄 Updating IntelliSense database..."
echo "📁 Project root: $PROJECT_ROOT"

# Change to project root directory
cd "$PROJECT_ROOT" || {
    echo "❌ Failed to change to project root directory: $PROJECT_ROOT"
    exit 1
}

# Clean previous build to ensure all files are recompiled
echo "🧹 Cleaning previous build..."
make clean > /dev/null 2>&1

# Generate compile_commands.json using bear
echo "📋 Generating compile_commands.json..."
bear -- make > /dev/null 2>&1

# Also capture test compilation commands
echo "🧪 Capturing test compilation commands..."
bear --append -- make build-test > /dev/null 2>&1 || echo "Note: Test build may have failed, but capturing what we can"

if [ $? -eq 0 ]; then
    echo "🔧 Post-processing compile_commands.json to add system headers..."
    
    # Get clang version for system headers
    CLANG_VERSION=$(clang --version | head -n1 | sed 's/.*clang version \([0-9]*\).*/\1/')
    SYSTEM_HEADERS="/Library/Developer/CommandLineTools/usr/lib/clang/${CLANG_VERSION}/include"
    MACOS_SDK="/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include"
    CMDLINE_TOOLS="/Library/Developer/CommandLineTools/usr/include"
    
    # Create a temporary file to modify compile_commands.json
    TMP_FILE=$(mktemp)
    
    # Add system include paths to each compilation command
    jq --arg sys_headers "$SYSTEM_HEADERS" \
       --arg macos_sdk "$MACOS_SDK" \
       --arg cmdline_tools "$CMDLINE_TOOLS" \
       'map(
         .arguments += ["-I" + $sys_headers, "-I" + $macos_sdk, "-I" + $cmdline_tools] |
         .arguments += ["-isystem", $sys_headers, "-isystem", $macos_sdk, "-isystem", $cmdline_tools]
       )' compile_commands.json > "$TMP_FILE"
    
    # Replace original file
    mv "$TMP_FILE" compile_commands.json
    
    echo "✅ IntelliSense database updated successfully!"
    echo "📄 compile_commands.json generated with $(cat compile_commands.json | jq length) entries"
    echo "🔧 Added system header paths for standard library support"
    echo ""
    echo "💡 To apply changes:"
    echo "   1. Reload VS Code window (Cmd+Shift+P → 'Developer: Reload Window')"
    echo "   2. Or restart the C++ extension (Cmd+Shift+P → 'C/C++: Restart IntelliSense')"
    echo ""
    echo "🔍 Standard library functions like strcmp should now be properly recognized!"
else
    echo "❌ Failed to generate compile_commands.json"
    echo "Please check build configuration and try again"
    exit 1
fi
