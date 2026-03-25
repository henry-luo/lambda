#!/bin/bash
# Subshell and command substitution tests

# Command substitution with $()
result=$(echo "hello")
echo "$result"

# Command substitution in arithmetic
count=$(echo 5)
echo $(( count * 3 ))

# Nested command substitution
inner=$(echo $(echo "nested"))
echo "$inner"

# Subshell does not affect parent scope
x=10
( x=20; echo "inside: $x" )
echo "outside: $x"

# Command substitution captures stdout
lines=$(printf "a\nb\nc")
echo "$lines"

# Subshell with loop
total=0
for i in 1 2 3; do
    total=$(( total + i ))
done
echo "total: $total"

# Command substitution in string
user="Lambda"
echo "Hello, $(echo $user)!"
