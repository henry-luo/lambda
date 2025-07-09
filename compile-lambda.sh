#!/bin/bash

# Enhanced unified compilation script with cross-compilation support
# Usage: ./compile-lambda.sh [config_file] [--platform=PLATFORM]
#        ./compile-lambda.sh --help

# Configuration file default
CONFIG_FILE="build_lambda_config.json"
PLATFORM=""

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --platform=*)
            PLATFORM="${1#*=}"
            shift
            ;;
        --platform)
            PLATFORM="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [config_file] [--platform=PLATFORM]"
            echo ""
            echo "Arguments:"
            echo "  config_file           JSON configuration file (default: build_lambda_config.json)"
            echo "  --platform=PLATFORM   Target platform for cross-compilation"
            echo ""
            echo "Available platforms:"
            echo "  windows               Cross-compile for Windows using MinGW"
            echo "  (none)                Native compilation for current platform"
            echo ""
            echo "Configuration files:"
            echo "  build_lambda_config.json    Compile lambda project"
            echo "  build_radiant_config.json   Compile radiant project"
            echo ""
            echo "Examples:"
            echo "  $0                                       # Native lambda compilation"
            echo "  $0 build_radiant_config.json            # Native radiant compilation"
            echo "  $0 --platform=windows                   # Cross-compile lambda for Windows"
            echo "  $0 build_radiant_config.json --platform=windows"
            exit 0
            ;;
        --*)
            echo "Error: Unknown option $1"
            echo "Use --help for usage information"
            exit 1
            ;;
        *)
            CONFIG_FILE="$1"
            shift
            ;;
    esac
done

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file '$CONFIG_FILE' not found!"
    exit 1
fi

# Function to extract values from JSON using jq or basic parsing
get_json_value() {
    local key="$1"
    local file="$2"
    local platform_prefix="$3"
    
    if command -v jq >/dev/null 2>&1; then
        if [ -n "$platform_prefix" ]; then
            # Try platform-specific value first, fallback to default
            local platform_value=$(jq -r ".platforms.$platform_prefix.$key // empty" "$file" 2>/dev/null)
            if [ -n "$platform_value" ] && [ "$platform_value" != "null" ]; then
                echo "$platform_value"
            else
                jq -r ".$key // empty" "$file" 2>/dev/null
            fi
        else
            jq -r ".$key // empty" "$file" 2>/dev/null
        fi
    else
        # Fallback to grep/sed for basic JSON parsing
        if [ -n "$platform_prefix" ]; then
            # Try platform-specific value first
            local platform_value=$(grep -A 20 "\"platforms\"" "$file" | grep -A 10 "\"$platform_prefix\"" | grep "\"$key\"" | head -1 | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*"([^"]*).*/\1/' | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*([^,}]*).*/\1/' | sed 's/[",]//g')
            if [ -n "$platform_value" ]; then
                echo "$platform_value"
                return
            fi
        fi
        grep "\"$key\"" "$file" | head -1 | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*"([^"]*).*/\1/' | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*([^,}]*).*/\1/' | sed 's/[",]//g'
    fi
}

# Function to extract array values from JSON
get_json_array() {
    local key="$1"
    local file="$2"
    local platform_prefix="$3"
    
    if command -v jq >/dev/null 2>&1; then
        if [ -n "$platform_prefix" ]; then
            # Try platform-specific array first, fallback to default
            local platform_array=$(jq -r ".platforms.$platform_prefix.$key[]? // empty" "$file" 2>/dev/null)
            if [ -n "$platform_array" ]; then
                echo "$platform_array"
            else
                jq -r ".$key[]? // empty" "$file" 2>/dev/null
            fi
        else
            jq -r ".$key[]? // empty" "$file" 2>/dev/null
        fi
    else
        # Fallback parsing for arrays
        if [ -n "$platform_prefix" ]; then
            # Try platform-specific array first
            local platform_section=$(sed -n '/"platforms"/,/}/p' "$file" | sed -n '/"'$platform_prefix'"/,/}/p')
            if echo "$platform_section" | grep -q "\"$key\""; then
                echo "$platform_section" | sed -n '/"'$key'":/,/]/p' | grep '"' | sed 's/.*"\([^"]*\)".*/\1/' | grep -v '^[[:space:]]*$' | grep -v "$key"
                return
            fi
        fi
        sed -n '/"'$key'":/,/]/p' "$file" | grep '"' | sed 's/.*"\([^"]*\)".*/\1/' | grep -v '^[[:space:]]*$' | grep -v "$key"
    fi
}

# Read configuration with platform override support
BUILD_DIR=$(get_json_value "build_dir" "$CONFIG_FILE" "$PLATFORM")
OUTPUT=$(get_json_value "output" "$CONFIG_FILE" "$PLATFORM")
DEBUG=$(get_json_value "debug" "$CONFIG_FILE" "$PLATFORM")
CROSS_COMPILE=$(get_json_value "cross_compile" "$CONFIG_FILE" "$PLATFORM")
TARGET_TRIPLET=$(get_json_value "target_triplet" "$CONFIG_FILE" "$PLATFORM")

# Validate required fields
if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR="build"  # Default fallback
fi

if [ -z "$OUTPUT" ]; then
    OUTPUT="lambda.exe"  # Default fallback
fi

# Set cross-compilation if platform is specified
if [ -n "$PLATFORM" ]; then
    if [ "$PLATFORM" = "windows" ]; then
        CROSS_COMPILE="true"
        if [ -z "$TARGET_TRIPLET" ]; then
            TARGET_TRIPLET="x86_64-w64-mingw32"
        fi
    fi
fi

# Set compilers based on cross-compilation target
if [ "$CROSS_COMPILE" = "true" ] && [ -n "$TARGET_TRIPLET" ]; then
    echo "Cross-compiling for target: $TARGET_TRIPLET"
    CC="${TARGET_TRIPLET}-gcc"
    CXX="${TARGET_TRIPLET}-g++"
    
    # Check if cross-compiler exists
    if ! command -v "$CC" >/dev/null 2>&1; then
        echo "Error: Cross-compiler '$CC' not found!"
        echo "Install it with: brew install mingw-w64"
        exit 1
    fi
    
    if ! command -v "$CXX" >/dev/null 2>&1; then
        echo "Error: Cross-compiler '$CXX' not found!"
        echo "Install it with: brew install mingw-w64"
        exit 1
    fi
else
    CC="clang"
    CXX="clang++"
fi

echo "Using C compiler: $CC"
echo "Using C++ compiler: $CXX"

# Create build directory
mkdir -p "$BUILD_DIR"

# Build compiler flags
INCLUDES=""
LIBS=""
LINK_LIBS=""
WARNINGS=""
FLAGS=""

# Process libraries with platform override support
if command -v jq >/dev/null 2>&1; then
    # Extract library information using jq
    if [ -n "$PLATFORM" ]; then
        # Use platform-specific libraries if available
        while IFS= read -r lib_info; do
            name=$(echo "$lib_info" | jq -r '.name')
            include=$(echo "$lib_info" | jq -r '.include // empty')
            lib=$(echo "$lib_info" | jq -r '.lib // empty')
            link=$(echo "$lib_info" | jq -r '.link // "static"')
            
            [ -n "$include" ] && INCLUDES="$INCLUDES -I$include"
            
            if [ "$link" = "static" ]; then
                [ -n "$lib" ] && LIBS="$LIBS $lib"
            else
                [ -n "$lib" ] && LINK_LIBS="$LINK_LIBS -L$lib -l$name"
            fi
        done < <(jq -c ".platforms.$PLATFORM.libraries[]? // .libraries[]?" "$CONFIG_FILE")
    else
        # Use default libraries
        while IFS= read -r lib_info; do
            name=$(echo "$lib_info" | jq -r '.name')
            include=$(echo "$lib_info" | jq -r '.include // empty')
            lib=$(echo "$lib_info" | jq -r '.lib // empty')
            link=$(echo "$lib_info" | jq -r '.link // "static"')
            
            [ -n "$include" ] && INCLUDES="$INCLUDES -I$include"
            
            if [ "$link" = "static" ]; then
                [ -n "$lib" ] && LIBS="$LIBS $lib"
            else
                [ -n "$lib" ] && LINK_LIBS="$LINK_LIBS -L$lib -l$name"
            fi
        done < <(jq -c '.libraries[]?' "$CONFIG_FILE")
    fi
    
    # Process warnings with platform override
    while IFS= read -r warning; do
        [ -n "$warning" ] && WARNINGS="$WARNINGS -Werror=$warning"
    done < <(get_json_array "warnings" "$CONFIG_FILE" "$PLATFORM")
    
    # Process flags with platform override
    while IFS= read -r flag; do
        [ -n "$flag" ] && FLAGS="$FLAGS -$flag"
    done < <(get_json_array "flags" "$CONFIG_FILE" "$PLATFORM")
    
    # Process linker flags with platform override
    LINKER_FLAGS=""
    while IFS= read -r flag; do
        [ -n "$flag" ] && LINKER_FLAGS="$LINKER_FLAGS -$flag"
    done < <(get_json_array "linker_flags" "$CONFIG_FILE" "$PLATFORM")
    
    # Get source files (using arrays compatible with older bash)
    SOURCE_FILES_ARRAY=()
    CPP_FILES_ARRAY=()
    
    # Read source files into array
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(jq -r '.source_files[]?' "$CONFIG_FILE" 2>/dev/null)
    
    # Read cpp files into array
    while IFS= read -r line; do
        [ -n "$line" ] && CPP_FILES_ARRAY+=("$line")
    done < <(jq -r '.cpp_files[]?' "$CONFIG_FILE" 2>/dev/null)
else
    # Fallback without jq - basic parsing
    if [ "$CROSS_COMPILE" = "true" ] || [ "$PLATFORM" = "windows" ]; then
        echo "Warning: jq not found, using fallback parsing for cross-compilation"
        INCLUDES="-Ilambda/tree-sitter/lib/include -Ilambda/tree-sitter-lambda/bindings/c -Iwindows-deps/include"
        LIBS="lambda/tree-sitter/libtree-sitter-windows.a lambda/tree-sitter-lambda/libtree-sitter-lambda.a windows-deps/lib/libmir.a windows-deps/lib/libzlog.a windows-deps/lib/liblexbor_static.a windows-deps/lib/libgmp.a"
        LINK_LIBS=""
        WARNINGS="-Wformat -Wincompatible-pointer-types -Wmultichar"
        FLAGS="-fms-extensions -static -DCROSS_COMPILE -D_WIN32"
        LINKER_FLAGS="-static-libgcc -static-libstdc++"
    else
        INCLUDES="-Ilambda/tree-sitter/lib/include -Ilambda/tree-sitter-lambda/bindings/c -I/usr/local/include -I/opt/homebrew/include"
        LIBS="lambda/tree-sitter/libtree-sitter.a lambda/tree-sitter-lambda/libtree-sitter-lambda.a /usr/local/lib/libmir.a /usr/local/lib/libzlog.a /usr/local/lib/liblexbor_static.a"
        LINK_LIBS="-L/opt/homebrew/lib -lgmp"
        WARNINGS="-Werror=format -Werror=incompatible-pointer-types -Werror=multichar"
        FLAGS="-fms-extensions -pedantic -fcolor-diagnostics"
        LINKER_FLAGS=""
    fi
    
    # Parse source files into arrays
    SOURCE_FILES_ARRAY=()
    CPP_FILES_ARRAY=()
    
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(get_json_array "source_files" "$CONFIG_FILE")
    
    while IFS= read -r line; do
        [ -n "$line" ] && CPP_FILES_ARRAY+=("$line")
    done < <(get_json_array "cpp_files" "$CONFIG_FILE")
fi

# Add debug flag if enabled
if [ "$DEBUG" = "true" ]; then
    FLAGS="$FLAGS -g"
fi

# Debug output
echo "Configuration loaded from: $CONFIG_FILE"
if [ -n "$PLATFORM" ]; then
    echo "Target platform: $PLATFORM"
fi
echo "Cross-compilation: $CROSS_COMPILE"
echo "Build directory: $BUILD_DIR"
echo "Output executable: $OUTPUT"
echo "Source files count: ${#SOURCE_FILES_ARRAY[@]}"
echo "C++ files count: ${#CPP_FILES_ARRAY[@]}"
echo

# Compile each C source file individually
output=""
OBJECT_FILES=""
compilation_success=true

for source in "${SOURCE_FILES_ARRAY[@]}"; do
    if [ -f "$source" ]; then
        # Generate object file name from source file
        obj_name=$(basename "$source" | sed 's/\.[^.]*$//')
        obj_file="$BUILD_DIR/${obj_name}.o"
        OBJECT_FILES="$OBJECT_FILES $obj_file"
        
        echo "Compiling: $source -> $obj_file"
        
        # Compile individual source file
        compile_output=$($CC $INCLUDES \
          -c "$source" -o "$obj_file" \
          $WARNINGS $FLAGS 2>&1)
        
        # Capture output and check for errors
        if [ $? -ne 0 ]; then
            compilation_success=false
        fi
        
        # Append to total output
        if [ -n "$compile_output" ]; then
            output="$output\n$compile_output"
        fi
    else
        echo "Warning: Source file '$source' not found"
        compilation_success=false
    fi
done

# Compile C++ files if any
if [ ${#CPP_FILES_ARRAY[@]} -gt 0 ]; then
    for cpp_file in "${CPP_FILES_ARRAY[@]}"; do
        if [ -f "$cpp_file" ]; then
            # Generate object file name from cpp file
            cpp_obj_name=$(basename "$cpp_file" | sed 's/\.[^.]*$//')
            cpp_obj_file="$BUILD_DIR/${cpp_obj_name}.o"
            OBJECT_FILES="$OBJECT_FILES $cpp_obj_file"
            
            echo "Compiling C++: $cpp_file -> $cpp_obj_file"
            
            # Use C++ specific flags (remove incompatible-pointer-types warning for C++)
            CPP_WARNINGS=$(echo "$WARNINGS" | sed 's/-Werror=incompatible-pointer-types//g' | sed 's/-Wincompatible-pointer-types//g')
            CPP_FLAGS="$FLAGS"
            if [ "$CROSS_COMPILE" = "true" ]; then
                CPP_FLAGS="$CPP_FLAGS -std=c++11 -fpermissive -Wno-fpermissive"
            fi
            
            cpp_output=$($CXX $INCLUDES -c "$cpp_file" -o "$cpp_obj_file" $CPP_WARNINGS $CPP_FLAGS 2>&1)
            if [ $? -ne 0 ]; then
                compilation_success=false
            fi
            if [ -n "$cpp_output" ]; then
                output="$output\n$cpp_output"
            fi
        else
            echo "Warning: C++ file '$cpp_file' not found"
            compilation_success=false
        fi
    done
fi

# Link final executable only if compilation was successful
if [ "$compilation_success" = true ]; then
    echo "Linking: $OUTPUT"
    link_output=$($CXX $OBJECT_FILES $LIBS $LINK_LIBS $LINKER_FLAGS -o "$OUTPUT" 2>&1)
    if [ $? -ne 0 ]; then
        compilation_success=false
    fi
    if [ -n "$link_output" ]; then
        output="$output\n$link_output"
    fi
fi

# Output the captured messages
echo -e "$output"

# Count errors and warnings (ignores color codes)
num_errors=$(echo "$output" | grep -c "error:")
num_warnings=$(echo "$output" | grep -c "warning:")

# Print summary with optional coloring
RED="\033[0;31m"
YELLOW="\033[1;33m"
GREEN="\033[0;32m"
RESET="\033[0m"

echo
echo -e "${YELLOW}Summary:${RESET}"
if [ "$num_errors" -gt 0 ]; then
    echo -e "${RED}Errors:   $num_errors${RESET}"
else
    echo -e "Errors:   $num_errors"
fi
echo -e "${YELLOW}Warnings: $num_warnings${RESET}"

# Final build status
if [ "$compilation_success" = true ]; then
    echo -e "${GREEN}Build successful: $OUTPUT${RESET}"
    
    # Show file info for cross-compiled binaries
    if [ "$CROSS_COMPILE" = "true" ]; then
        echo "File type information:"
        file "$OUTPUT" 2>/dev/null || echo "file command not available"
    fi
    
    exit 0
else
    echo -e "${RED}Build failed${RESET}"
    exit 1
fi


