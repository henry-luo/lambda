#!/bin/bash
# Special variables tests

# $# — number of arguments (script level, no args)
echo "argc: $#"

# Positional parameters via function
show_args() {
    echo "count: $#"
    echo "first: $1"
    echo "second: $2"
    echo "all: $@"
    echo "all-star: $*"
}
show_args "hello" "world" "test"

# $? — already tested, but verify here
true
echo "exit: $?"

# Shift
shift_test() {
    echo "before: $1 $2 $3"
    shift
    echo "after: $1 $2"
    shift
    echo "final: $1"
}
shift_test "a" "b" "c"

# Default handling with ${N:-default}
use_defaults() {
    local val="${1:-default_value}"
    echo "$val"
}
use_defaults "provided"
use_defaults
