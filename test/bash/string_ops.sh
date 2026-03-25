#!/bin/bash
# String operations tests

# String comparison
a="hello"
b="hello"
c="world"

if [ "$a" = "$b" ]; then
    echo "a equals b"
fi

if [ "$a" != "$c" ]; then
    echo "a not equals c"
fi

# String length via expr
echo "${#a}"

# String concatenation
first="Hello"
second="World"
combined="$first, $second!"
echo "$combined"

# Multi-line string (here-string style)
msg="Line one
Line two
Line three"
echo "$msg"

# Empty string check
empty=""
if [ -z "$empty" ]; then
    echo "string is empty"
fi

notempty="data"
if [ -n "$notempty" ]; then
    echo "string is not empty"
fi

# String in arithmetic context
num_str="42"
result=$(( num_str + 8 ))
echo "$result"

# Quoting preserves spaces
spaced="  hello  world  "
echo "$spaced"
echo "[$spaced]"
