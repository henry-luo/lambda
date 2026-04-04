#!/bin/bash
# Test: source / . command (Phase 7.1)

# Source a library using 'source' keyword
source test/bash/source/source_lib.sh

# Variables from sourced script are available
echo "version: $LIB_VERSION"

# Functions from sourced script are available
greet "World"

# Function calling arithmetic
result=$(add_numbers 3 4)
echo "3 + 4 = $result"

# Source again using '.' syntax - should work too
. test/bash/source/source_lib.sh

# Variables still accessible
echo "prefix: $LIB_PREFIX"

echo "done"
