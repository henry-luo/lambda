#!/bin/bash
# Arithmetic expansion tests

# Basic operations
echo $(( 10 + 20 ))
echo $(( 100 - 37 ))
echo $(( 7 * 6 ))
echo $(( 100 / 4 ))
echo $(( 17 % 5 ))

# Variables in arithmetic
a=10
b=25
echo $(( a + b ))
echo $(( a * b ))
echo $(( b - a ))
echo $(( b / a ))

# Nested arithmetic
echo $(( (3 + 4) * 2 ))
echo $(( 2 ** 8 ))

# Increment/decrement patterns
x=5
echo $(( x + 1 ))
echo $(( x - 1 ))

# Compound expressions
echo $(( 1 + 2 + 3 + 4 + 5 ))

# Negative numbers
echo $(( -10 + 3 ))
echo $(( 5 * -2 ))
