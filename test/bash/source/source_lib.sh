#!/bin/bash
# Helper library for source_cmd test
# This file is sourced by source_cmd.sh

LIB_VERSION="1.0"
LIB_PREFIX="[lib]"

greet() {
    local name="$1"
    echo "$LIB_PREFIX Hello, $name!"
}

add_numbers() {
    local a="$1"
    local b="$2"
    echo $((a + b))
}
