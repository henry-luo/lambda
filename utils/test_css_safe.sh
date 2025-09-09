#!/bin/bash

# Safe CSS test runner with memory limits and timeouts
# This script prevents system hangs from memory leaks or infinite loops

set -e

echo "Building CSS minimal tests..."

# Compile the minimal CSS tests
clang -I. -I./lib/mem-pool/include -I./lambda/input \
      -I/opt/homebrew/Cellar/criterion/2.4.2_2/include \
      -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib \
      -lcriterion -o test_css_minimal \
      test/test_css_minimal.c \
      lambda/input/css_tokenizer.c \
      lambda/input/css_parser.c \
      lambda/input/css_properties.c \
      lib/mem-pool/src/variable.c \
      lib/mem-pool/src/buffer.c \
      lib/mem-pool/src/utils.c \
      -lm

echo "Running CSS tests with safety limits..."

# Set memory limits using multiple approaches
echo "Setting memory limits..."

# Try different memory limiting approaches (some may not work on all systems)
# 1. Try ulimit for virtual memory (100MB)
ulimit -v 102400 2>/dev/null || echo "ulimit -v not supported on this system"

# 2. Try ulimit for resident set size (50MB) 
ulimit -m 51200 2>/dev/null || echo "ulimit -m not supported on this system"

# 3. Use timeout as fallback protection (30 seconds)
echo "Running tests with 30-second timeout and memory limits..."
timeout 30s ./test_css_minimal --verbose

echo "CSS tests completed successfully!"

# Clean up
rm -f test_css_minimal

echo "Test executable cleaned up."
