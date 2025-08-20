#!/bin/bash

# Convenience script for Windows native compilation
# This script sets up the environment and calls the main compile script

# Check MSYS2 environment
if [[ "$MSYSTEM" != "MSYS" && "$MSYSTEM" != "MINGW64" && "$MSYSTEM" != "CLANG64" ]]; then
    echo "Warning: This script should be run in MSYS2 environment"
    echo "Current MSYSTEM: ${MSYSTEM:-not set}"
fi

# Set up compiler based on MSYS2 environment
if [[ "$MSYSTEM" == "CLANG64" ]]; then
    export CC="clang"
    export CXX="clang++"
    export AR="llvm-ar"
    export RANLIB="llvm-ranlib"
elif [[ "$MSYSTEM" == "MINGW64" ]]; then
    export CC="gcc"
    export CXX="g++"
    export AR="ar"
    export RANLIB="ranlib"
else
    # Add CLANG64 bin directory to PATH and set explicit paths
    export PATH="/clang64/bin:$PATH"
    export CC="/clang64/bin/clang.exe"
    export CXX="/clang64/bin/clang++.exe"
    export AR="/clang64/bin/llvm-ar.exe"
    export RANLIB="/clang64/bin/llvm-ranlib.exe"
fi

# Set native Windows build environment
export NATIVE_WINDOWS_BUILD=1

echo "Windows Native Build Environment:"
echo "  MSYSTEM: ${MSYSTEM:-not set}"
echo "  CC: ${CC:-default}"
echo "  CXX: ${CXX:-default}"
echo ""

# Call main compile script with native Windows config
exec ./compile.sh build_lambda_win_native_config.json "$@"
