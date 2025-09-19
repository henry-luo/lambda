#!/bin/bash
# Switch to MINGW64 environment and build
# This script ensures we're using the MINGW64 toolchain instead of CLANG64

echo "🔄 Switching to MINGW64 environment for Windows build..."

# Check current MSYSTEM
echo "Current MSYSTEM: ${MSYSTEM:-not set}"

if [[ "$MSYSTEM" != "MINGW64" ]]; then
    echo "⚠️  Warning: Not in MINGW64 environment!"
    echo "💡 To switch to MINGW64:"
    echo "   1. Close this terminal"
    echo "   2. Open MSYS2 MINGW64 terminal (not CLANG64)"
    echo "   3. Navigate to: cd /d/Projects/Lambda"
    echo "   4. Run this script again"
    echo ""
    echo "Or, if you have MINGW64 available, run:"
    echo "   /mingw64/bin/bash $0"
    exit 1
fi

# Verify MINGW64 tools
echo "🔍 Verifying MINGW64 toolchain..."
which gcc || { echo "❌ GCC not found in MINGW64"; exit 1; }
which g++ || { echo "❌ G++ not found in MINGW64"; exit 1; }

echo "✅ MINGW64 environment confirmed"
echo "📦 Available tools:"
echo "   GCC: $(gcc --version | head -1)"
echo "   G++: $(g++ --version | head -1)"
echo ""

# Set environment variables for MINGW64
export CC="gcc"
export CXX="g++"
export AR="ar"
export RANLIB="ranlib"
export PATH="/mingw64/bin:$PATH"

echo "🏗️  Building with MINGW64 (should avoid Universal CRT)..."
make build

echo ""
echo "🧪 Testing executable..."
./lambda.exe --help

echo ""
echo "📋 Checking DLL dependencies..."
ldd lambda.exe | grep -E "not found|mingw64|msys64|ucrt|api-ms-win-crt"
