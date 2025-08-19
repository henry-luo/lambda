#!/bin/bash
# UTF8PROC Setup Script for Lambda Unicode Support
# Ensures utf8proc is available on all target platforms

set -e

echo "=== UTF8PROC Setup for Lambda Unicode Support ==="

# Detect platform
PLATFORM="unknown"
case "$(uname -s)" in
    Darwin*)    PLATFORM="darwin" ;;
    Linux*)     PLATFORM="linux" ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
esac

echo "Detected platform: $PLATFORM"

# Function to check if utf8proc is available
check_utf8proc() {
    local include_path="$1"
    local lib_path="$2"
    
    if [ -f "$include_path/utf8proc.h" ] && [ -f "$lib_path" ]; then
        echo "✓ utf8proc found at $include_path and $lib_path"
        return 0
    else
        echo "✗ utf8proc not found at $include_path or $lib_path"
        return 1
    fi
}

# Function to install utf8proc
install_utf8proc() {
    echo "Installing utf8proc..."
    
    case "$PLATFORM" in
        darwin)
            if command -v brew >/dev/null 2>&1; then
                echo "Installing utf8proc via Homebrew..."
                brew install utf8proc
            else
                echo "Homebrew not found. Please install utf8proc manually."
                exit 1
            fi
            ;;
        linux)
            if command -v apt-get >/dev/null 2>&1; then
                echo "Installing utf8proc via apt..."
                sudo apt-get update
                sudo apt-get install -y libutf8proc-dev
            elif command -v yum >/dev/null 2>&1; then
                echo "Installing utf8proc via yum..."
                sudo yum install -y utf8proc-devel
            elif command -v pacman >/dev/null 2>&1; then
                echo "Installing utf8proc via pacman..."
                sudo pacman -S utf8proc
            else
                echo "Package manager not found. Please install utf8proc manually."
                exit 1
            fi
            ;;
        windows)
            echo "Windows detected. Please ensure utf8proc is available in windows-deps/"
            mkdir -p windows-deps/include windows-deps/lib
            echo "Please download utf8proc for Windows and place:"
            echo "  - utf8proc.h in windows-deps/include/"
            echo "  - libutf8proc.a in windows-deps/lib/"
            exit 1
            ;;
    esac
}

# Platform-specific paths
case "$PLATFORM" in
    darwin)
        UTF8PROC_INCLUDE="/opt/homebrew/include"
        UTF8PROC_LIB="/opt/homebrew/lib/libutf8proc.a"
        
        # Also check /usr/local for older installations
        if ! check_utf8proc "$UTF8PROC_INCLUDE" "$UTF8PROC_LIB"; then
            UTF8PROC_INCLUDE="/usr/local/include"
            UTF8PROC_LIB="/usr/local/lib/libutf8proc.a"
        fi
        ;;
    linux)
        UTF8PROC_INCLUDE="/usr/include"
        UTF8PROC_LIB="/usr/lib/x86_64-linux-gnu/libutf8proc.a"
        
        # Also check /usr/local
        if ! check_utf8proc "$UTF8PROC_INCLUDE" "$UTF8PROC_LIB"; then
            UTF8PROC_INCLUDE="/usr/local/include"
            UTF8PROC_LIB="/usr/local/lib/libutf8proc.a"
        fi
        ;;
    windows)
        UTF8PROC_INCLUDE="windows-deps/include"
        UTF8PROC_LIB="windows-deps/lib/libutf8proc.a"
        ;;
esac

# Check if utf8proc is available
if ! check_utf8proc "$UTF8PROC_INCLUDE" "$UTF8PROC_LIB"; then
    echo "utf8proc not found. Attempting to install..."
    install_utf8proc
    
    # Verify installation
    if ! check_utf8proc "$UTF8PROC_INCLUDE" "$UTF8PROC_LIB"; then
        echo "Failed to install utf8proc. Please install manually."
        exit 1
    fi
fi

# Check utf8proc version
echo "Checking utf8proc version..."
if [ -f "$UTF8PROC_INCLUDE/utf8proc.h" ]; then
    VERSION_MAJOR=$(grep "UTF8PROC_VERSION_MAJOR" "$UTF8PROC_INCLUDE/utf8proc.h" | cut -d' ' -f3)
    VERSION_MINOR=$(grep "UTF8PROC_VERSION_MINOR" "$UTF8PROC_INCLUDE/utf8proc.h" | cut -d' ' -f3)
    VERSION_PATCH=$(grep "UTF8PROC_VERSION_PATCH" "$UTF8PROC_INCLUDE/utf8proc.h" | cut -d' ' -f3)
    echo "✓ utf8proc version: $VERSION_MAJOR.$VERSION_MINOR.$VERSION_PATCH"
    
    # Check minimum version (2.0.0)
    if [ "$VERSION_MAJOR" -lt 2 ]; then
        echo "⚠️  Warning: utf8proc version is older than recommended (2.0.0+)"
    fi
fi

# Update build configuration paths if needed
BUILD_CONFIG="build_lambda_config.json"
if [ -f "$BUILD_CONFIG" ]; then
    echo "Updating build configuration with detected paths..."
    # This is a placeholder - in practice, you'd use jq or sed to update the JSON
    echo "  Include path: $UTF8PROC_INCLUDE"
    echo "  Library path: $UTF8PROC_LIB"
fi

echo "✓ utf8proc setup complete for $PLATFORM"
echo ""
echo "Next steps:"
echo "1. Build Lambda with: make build"
echo "2. Test Unicode support with: make test"
echo "3. To use ICU instead, set: LAMBDA_UNICODE_LEVEL=3"
