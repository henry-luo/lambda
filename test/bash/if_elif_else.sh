#!/bin/bash
# If/elif/else conditional tests

# Simple if
x=10
if [ $x -gt 5 ]; then
    echo "x is greater than 5"
fi

# If-else
y=3
if [ $y -gt 5 ]; then
    echo "y is greater than 5"
else
    echo "y is not greater than 5"
fi

# If-elif-else
z=7
if [ $z -gt 10 ]; then
    echo "z is greater than 10"
elif [ $z -gt 5 ]; then
    echo "z is greater than 5 but not greater than 10"
else
    echo "z is 5 or less"
fi

# String comparison
name="Lambda"
if [ "$name" = "Lambda" ]; then
    echo "name is Lambda"
fi

# Nested if
a=15
if [ $a -gt 10 ]; then
    if [ $a -lt 20 ]; then
        echo "a is between 10 and 20"
    fi
fi

# Negation
b=0
if [ ! $b -gt 5 ]; then
    echo "b is not greater than 5"
fi

# -z (empty string) and -n (non-empty string)
empty=""
notempty="hello"
if [ -z "$empty" ]; then
    echo "empty is empty"
fi
if [ -n "$notempty" ]; then
    echo "notempty is not empty"
fi
