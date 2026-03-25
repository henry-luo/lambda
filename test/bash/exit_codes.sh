#!/bin/bash
# Exit code and $? tests

# Successful command
true
echo $?

# Failed command
false
echo $?

# Custom exit code from function
returns_two() {
    return 2
}
returns_two
echo $?

# Exit code after test
[ 5 -gt 3 ]
echo $?

[ 3 -gt 5 ]
echo $?

# Exit code with && (short-circuit)
true && echo "and: true"
false && echo "and: should not print"

# Exit code with || (short-circuit)
false || echo "or: fallback"
true || echo "or: should not print"

# Chained && and ||
true && true && echo "chain: all true"
true && false || echo "chain: fallback after false"
