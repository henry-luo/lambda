#!/bin/bash
# declare builtin tests

# declare -i: integer attribute
declare -i num
num=42
echo "$num"

# integer coercion on assignment
declare -i x
x="25abc"
echo "$x"

# declare -r: readonly variable
declare -r constant="immutable"
echo "$constant"

# declare -l: lowercase
declare -l lower
lower="HELLO WORLD"
echo "$lower"

# declare -u: uppercase
declare -u upper
upper="hello world"
echo "$upper"

# declare -a: indexed array
declare -a fruits
fruits[0]="apple"
fruits[1]="banana"
echo "${fruits[0]}"
echo "${#fruits[@]}"

# declare with value
declare -i count=100
echo "$count"

# Combined flags: declare -ir (integer + readonly)
declare -ir MAX=999
echo "$MAX"
