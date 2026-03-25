#!/bin/bash
# Test expression / [[ ]] tests

# Numeric comparisons with [[ ]]
x=10
if [[ $x -eq 10 ]]; then echo "eq pass"; fi
if [[ $x -ne 5 ]]; then echo "ne pass"; fi
if [[ $x -gt 5 ]]; then echo "gt pass"; fi
if [[ $x -ge 10 ]]; then echo "ge pass"; fi
if [[ $x -lt 20 ]]; then echo "lt pass"; fi
if [[ $x -le 10 ]]; then echo "le pass"; fi

# String comparisons with [[ ]]
s="hello"
if [[ "$s" == "hello" ]]; then echo "str eq pass"; fi
if [[ "$s" != "world" ]]; then echo "str ne pass"; fi
if [[ "$s" < "world" ]]; then echo "str lt pass"; fi
if [[ "$s" > "abc" ]]; then echo "str gt pass"; fi

# Logical operators
a=5
b=15
if [[ $a -lt 10 && $b -gt 10 ]]; then
    echo "and pass"
fi

if [[ $a -gt 10 || $b -gt 10 ]]; then
    echo "or pass"
fi

if [[ ! $a -gt 10 ]]; then
    echo "not pass"
fi

# Regex match
text="Hello123"
if [[ "$text" =~ ^Hello[0-9]+$ ]]; then
    echo "regex pass"
fi

# Pattern match (glob)
file="script.sh"
if [[ "$file" == *.sh ]]; then
    echo "glob pass"
fi
