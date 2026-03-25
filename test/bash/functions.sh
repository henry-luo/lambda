#!/bin/bash
# Function definition and call tests

# Basic function
greet() {
    echo "Hello from function"
}
greet

# Function with parameters
add() {
    echo $(( $1 + $2 ))
}
add 3 4
add 10 20

# Function with local variables
counter() {
    local count=0
    count=$(( count + $1 ))
    echo "$count"
}
counter 5
counter 10

# Function with return value (exit code)
is_positive() {
    if [ $1 -gt 0 ]; then
        return 0
    else
        return 1
    fi
}
is_positive 5
echo $?
is_positive -3
echo $?

# Function calling another function
multiply() {
    echo $(( $1 * $2 ))
}
double() {
    multiply $1 2
}
double 7
double 15

# Recursive function (factorial)
factorial() {
    if [ $1 -le 1 ]; then
        echo 1
    else
        local prev=$(factorial $(( $1 - 1 )))
        echo $(( $1 * prev ))
    fi
}
factorial 1
factorial 5
factorial 6

# Function keyword syntax
function say_hello {
    echo "hello $1"
}
say_hello "world"
say_hello "Lambda"
