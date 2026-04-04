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

# Bitwise operations
r=$(( 255 & 15 )); echo $r
r=$(( 15 | 240 )); echo $r
echo $(( 5 ^ 3 ))
echo $(( ~0 ))
echo $(( 1 << 8 ))
echo $(( 256 >> 4 ))

# Comparison operators
echo $(( 10 == 10 ))
echo $(( 10 != 5 ))
echo $(( 3 < 5 ))
echo $(( 5 > 3 ))
echo $(( 3 <= 3 ))
echo $(( 5 >= 10 ))

# Logical operators
t=1
f=0
a=$(( t && t )); echo $a
b=$(( t && f )); echo $b
c=$(( f || t )); echo $c
d=$(( f || f )); echo $d
echo $(( !0 ))
echo $(( !1 ))
echo $(( !42 ))

# Ternary operator
echo $(( 1 ? 10 : 20 ))
echo $(( 0 ? 10 : 20 ))
x=5
echo $(( x > 3 ? 100 : 200 ))
echo $(( x < 3 ? 100 : 200 ))

# Post-increment/decrement
y=10
echo $(( y++ ))
echo $y
echo $(( y-- ))
echo $y

# Pre-increment/decrement
z=10
echo $(( ++z ))
echo $z
echo $(( --z ))
echo $z
