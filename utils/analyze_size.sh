#!/bin/bash

# Lambda Script Executable Size Analysis
# This script analyzes the components that contribute to lambda.exe size

echo "=== LAMBDA SCRIPT EXECUTABLE SIZE BREAKDOWN ==="
echo ""
echo "Generated on: $(date)"
echo ""

# Basic size info
echo "=== EXECUTABLE SIZE ==="
ls -lah lambda*.exe | awk '{print $9 ": " $5}' | sort
echo ""

# Object files analysis
echo "=== LARGEST OBJECT FILES ==="
printf "%-40s %10s\n" "Object File" "Size (KB)"
printf "%-40s %10s\n" "----------------------------------------" "----------"
ls -la build/*.o | sort -k5 -nr | head -15 | awk '{printf "%-40s %10.1f\n", substr($9, index($9, "/")+1), $5/1024}'
echo ""

# Calculate total object size
TOTAL_OBJ_SIZE=$(ls -la build/*.o | awk '{sum += $5} END {print sum/1024/1024}')
echo "Total object files size: ${TOTAL_OBJ_SIZE} MB"
echo ""

# Static libraries breakdown
echo "=== STATIC LIBRARIES ANALYSIS ==="
echo "Note: This shows library file sizes on disk, not what's actually linked into lambda.exe"
printf "%-50s %10s %12s\n" "Library" "Size (MB)" "Status"
printf "%-50s %10s %12s\n" "--------------------------------------------------" "----------" "------------"

# Function to get library size and check if actually linked
get_lib_info() {
    local lib_file="$1"
    local lib_name="$2"
    local search_pattern="$3"
    
    if [ -f "$lib_file" ]; then
        local size=$(ls -la "$lib_file" | awk '{printf "%.1f", $5/1024/1024}')
        # Check if symbols from this library are actually in the binary
        if nm lambda.exe 2>/dev/null | grep -q "$search_pattern"; then
            printf "%10s %12s\n" "$size" "LINKED"
        else
            printf "%10s %12s\n" "$size" "NOT LINKED"
        fi
    else
        printf "%10s %12s\n" "N/A" "NOT FOUND"
    fi
}

echo "libcrypto.a (OpenSSL encryption)                 $(get_lib_info "/opt/homebrew/Cellar/openssl@3/3.5.2/lib/libcrypto.a" "crypto" "SSL_")"
echo "libginac.a (symbolic computation)                $(get_lib_info "/opt/homebrew/Cellar/ginac/1.8.9/lib/libginac.a" "ginac" "GiNaC")"
echo "libcln.a (number theory)                         $(get_lib_info "/opt/homebrew/Cellar/cln/1.3.7/lib/libcln.a" "cln" "cln::")"
echo "libmir.a (JIT compiler)                          $(get_lib_info "/usr/local/lib/libmir.a" "mir" "MIR_")"
echo "libssl.a (OpenSSL TLS)                           $(get_lib_info "/opt/homebrew/Cellar/openssl@3/3.5.2/lib/libssl.a" "ssl" "SSL_")"
echo "libgmp.a (big integer math)                      $(get_lib_info "/opt/homebrew/Cellar/gmp/6.3.0/lib/libgmp.a" "gmp" "mpz_")"
echo "libcurl.a (HTTP client)                          $(get_lib_info "mac-deps/curl-8.10.1/lib/libcurl.a" "curl" "curl_")"
echo "libtree-sitter-lambda.a (Lambda parser)          $(get_lib_info "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" "ts-lambda" "ts_language_lambda")"
echo "libutf8proc.a (Unicode processing)               $(get_lib_info "/opt/homebrew/Cellar/utf8proc/2.10.0/lib/libutf8proc.a" "utf8proc" "utf8proc_")"
echo "libnghttp2.a (HTTP/2 support)                    $(get_lib_info "mac-deps/nghttp2/lib/libnghttp2.a" "nghttp2" "nghttp2_")"
echo "libedit.a (command line editing)                 $(get_lib_info "/opt/homebrew/opt/libedit/lib/libedit.a" "edit" "readline")"
echo "libcriterion.a (unit testing)                    $(get_lib_info "/opt/homebrew/Cellar/criterion/2.4.2_2/lib/libcriterion.a" "criterion" "criterion_")"
echo "libtree-sitter.a (parsing framework)             $(get_lib_info "lambda/tree-sitter/libtree-sitter.a" "ts" "ts_node_")"
echo "libmpdec.a (decimal arithmetic)                  $(get_lib_info "/opt/homebrew/Cellar/mpdecimal/4.0.1/lib/libmpdec.a" "mpdec" "mpd_")"
echo ""

# Component grouping
echo "=== FUNCTIONAL COMPONENTS ==="
echo "Math & Symbolic Computation:"
echo "  - GiNaC (symbolic algebra): ~5.4 MB"
echo "  - CLN (number theory): ~4.4 MB" 
echo "  - GMP (big integers): ~0.8 MB"
echo "  - mpdecimal (decimal math): ~0.2 MB"
echo "  Total: ~10.8 MB (52% of estimated size)"
echo ""
echo "Cryptography & Security:"
echo "  - OpenSSL crypto: ~8.0 MB"
echo "  - OpenSSL SSL/TLS: ~1.4 MB"
echo "  Total: ~9.4 MB (45% of estimated size)"
echo ""
echo "JIT Compilation:"
echo "  - MIR JIT compiler: ~3.3 MB (16% of estimated size)"
echo ""
echo "Network & HTTP:"
echo "  - libcurl: ~0.8 MB"
echo "  - nghttp2: ~0.3 MB"
echo "  Total: ~1.1 MB (5% of estimated size)"
echo ""
echo "Lambda Runtime:"
echo "  - Core objects: ~1.4 MB (7% of estimated size)"
echo "  - Tree-sitter parsing: ~0.7 MB (3% of estimated size)"
echo ""

# Optimization suggestions
echo "=== SIZE OPTIMIZATION OPPORTUNITIES ==="
echo ""
echo "1. HIGH IMPACT (Major size reduction):"
echo "   • Make math libraries optional: -10.8 MB potential"
echo "   • Use system OpenSSL instead of static: -9.4 MB potential"
echo "   • Conditional JIT compilation: -3.3 MB potential"
echo ""
echo "2. MEDIUM IMPACT:"
echo "   • Remove test framework from production: -0.2 MB"
echo "   • Use minimal HTTP client: -0.8 MB potential"
echo ""
echo "3. ALREADY OPTIMIZED:"
echo "   • Link-time optimization (LTO) enabled in release build"
echo "   • Symbol stripping enabled in release build"
echo "   • Release build is 5.9% smaller than debug build"
echo ""

# Build comparison
echo "=== BUILD SIZE COMPARISON ==="
if [ -f "lambda.exe" ] && [ -f "lambda_debug.exe" ] && [ -f "lambda_release.exe" ]; then
    echo "Regular build (O2):     $(ls -lah lambda.exe | awk '{print $5}')"
    echo "Debug build (O0+ASAN):  $(ls -lah lambda_debug.exe | awk '{print $5}')"
    echo "Release build (O3+LTO): $(ls -lah lambda_release.exe | awk '{print $5}')"
else
    echo "Not all builds are available. Run 'make build', 'make debug', and 'make release'."
fi
echo ""

echo "Analysis complete. For detailed symbol analysis, run:"
echo "  nm -S --size-sort lambda.exe | head -50"
echo "  objdump -h lambda.exe  # (if available)"
