#!/bin/bash
# posix_mode.sh — basic test for POSIX-compatible behavior
# In POSIX mode, `local` is not supported; variables should be global

x=global

modify_x() {
    x=modified
}

modify_x
echo "$x"

# basic arithmetic and string ops (POSIX-compatible)
a=10
b=3
echo $((a + b))
echo $((a * b))

# string operations
greeting="hello"
echo "${greeting} world"
echo "${#greeting}"
