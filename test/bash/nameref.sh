#!/bin/bash
# nameref (declare -n) tests

# Basic nameref: read through reference
x=42
declare -n ref=x
echo "$ref"

# Write through nameref
ref=100
echo "$x"

# Nameref to another variable
greeting="hello"
declare -n alias=greeting
alias="world"
echo "$greeting"

# Nameref chain (two levels)
a="deep"
declare -n b=a
declare -n c=b
echo "$c"

# Write through two-level chain
c="deeper"
echo "$a"

# Unset through nameref unsets the target
target="will be gone"
declare -n nref=target
unset nref
echo "${target:-unset}"

# Nameref in function scope
myfunc() {
    local -n fref=$1
    fref="from function"
}
result="original"
myfunc result
echo "$result"

# Nameref with integer attribute on target
declare -i ival=10
declare -n iref=ival
iref="5+5"
echo "$ival"
