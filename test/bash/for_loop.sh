#!/bin/bash
# For loop tests

# For-in with list
for item in apple banana cherry; do
    echo "$item"
done

# For-in with quoted strings
for word in "hello world" "foo bar"; do
    echo "$word"
done

# For-in with sequence via brace expansion
for i in 1 2 3 4 5; do
    echo "$i"
done

# C-style for loop
for (( i=0; i<5; i++ )); do
    echo "count: $i"
done

# Accumulator pattern
sum=0
for n in 10 20 30 40; do
    sum=$(( sum + n ))
done
echo "$sum"

# Nested for loops
for a in 1 2 3; do
    for b in x y; do
        echo "$a$b"
    done
done
