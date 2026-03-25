#!/bin/bash
# Parameter expansion tests

# Default value
unset missing
echo "${missing:-default_val}"
echo "${missing}"

# Assign default
unset assign_me
echo "${assign_me:=assigned}"
echo "${assign_me}"

# Alternate value
set_var="exists"
echo "${set_var:+alternate}"
unset unset_var
echo "${unset_var:+alternate}"

# String length
str="Hello, Lambda!"
echo "${#str}"

# Prefix removal (shortest)
path="/usr/local/bin/lambda"
echo "${path#*/}"

# Prefix removal (longest)
echo "${path##*/}"

# Suffix removal (shortest)
file="archive.tar.gz"
echo "${file%.*}"

# Suffix removal (longest)
echo "${file%%.*}"

# Pattern replacement (first)
text="hello world hello"
echo "${text/hello/goodbye}"

# Pattern replacement (all)
echo "${text//hello/goodbye}"

# Substring extraction
phrase="Hello, World!"
echo "${phrase:7:5}"
