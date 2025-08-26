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

if [ $? -eq 0 ]; then
    echo "✅ IntelliSense database updated successfully!"
    echo "📄 compile_commands.json generated with $(cat compile_commands.json | jq length) entries"
    echo ""
    echo "💡 To apply changes:"
    echo "   1. Reload VS Code window (Cmd+Shift+P → 'Developer: Reload Window')"
    echo "   2. Or restart the C++ extension (Cmd+Shift+P → 'C/C++: Restart IntelliSense')"
else
    echo "❌ Failed to generate compile_commands.json"
    echo "Please check build configuration and try again"
    exit 1
fi
