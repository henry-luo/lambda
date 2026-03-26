#!/bin/bash
# scope_stack.sh — tests dynamic scoping with local variables

# test: local var accumulates properly via global writes
counter=0

increment() {
    local n=$1
    counter=$((counter + n))
}

increment 5
increment 3
echo "counter=$counter"

# test: local shadows global, callee sees caller's local (dynamic scoping)
x=global

show_x() {
    echo "x=$x"
}

use_local() {
    local x=local
    show_x
    echo "inside=$x"
}

show_x
use_local
show_x

# test: recursion with local vars keeps each call's own n
fib() {
    local n=$1
    if [ "$n" -le 1 ]; then
        echo $n
        return
    fi
    local a=$(fib $((n - 1)))
    local b=$(fib $((n - 2)))
    echo $((a + b))
}

echo "fib(7)=$(fib 7)"

# test: local var not visible outside function
set_local() {
    local secret=42
}
set_local
echo "secret=${secret:-unset}"
