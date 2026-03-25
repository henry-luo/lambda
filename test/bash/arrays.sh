#!/bin/bash
# Indexed array tests

# Array declaration
arr=(alpha beta gamma delta)
echo "${arr[0]}"
echo "${arr[1]}"
echo "${arr[2]}"
echo "${arr[3]}"

# Array length
echo "${#arr[@]}"

# All elements
echo "${arr[@]}"

# Modify element
arr[1]="BETA"
echo "${arr[1]}"

# Append element
arr+=(epsilon)
echo "${#arr[@]}"
echo "${arr[4]}"

# Iterate over array
nums=(10 20 30 40 50)
sum=0
for n in "${nums[@]}"; do
    sum=$(( sum + n ))
done
echo "$sum"

# Array slicing
letters=(a b c d e f)
echo "${letters[@]:2:3}"

# Unset element
unset 'letters[1]'
echo "${letters[@]}"
