#!/bin/bash

# Test external command execution via posix_spawn

# Test 1: basic external command (ls)
echo "=== ls ==="
ls /dev/null
echo "exit: $?"

# Test 2: date command (just check it runs)
echo "=== date ==="
date +%Y > /dev/null
echo "exit: $?"

# Test 3: external command with arguments
echo "=== basename ==="
basename /usr/local/bin/test
echo "exit: $?"

# Test 4: command not found
echo "=== not found ==="
nonexistent_command_xyz 2>/dev/null
echo "exit: $?"

# Test 5: external in command substitution
echo "=== command substitution ==="
result=$(basename /path/to/myfile.txt)
echo "$result"

# Test 6: external in pipeline (builtin | external)
echo "=== pipeline builtin to external ==="
echo "hello world" | tr 'h' 'H'

# Test 7: external with absolute path
echo "=== absolute path ==="
/bin/echo "from bin echo"

# Test 8: external command captures stdout
echo "=== expr ==="
val=$(expr 3 + 4)
echo "result: $val"

# Test 9: external with multiple args
echo "=== printf external ==="
/usr/bin/printf "%s-%s\n" "hello" "world"

# Test 10: external to builtin pipeline
echo "=== seq to tail ==="
seq 1 5 | tail -n 3

# Test 11: external to builtin pipeline
echo "=== reverse sort ==="
seq 5 -1 1 | sort

# Test 12: external command exit code propagation
echo "=== exit codes ==="
/usr/bin/false
echo "false: $?"
/usr/bin/true
echo "true: $?"
