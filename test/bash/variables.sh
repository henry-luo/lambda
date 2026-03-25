#!/bin/bash
# Variable assignment and expansion

name="Lambda"
version="1.0"
echo "$name"
echo "$version"

# Concatenation via expansion
greeting="Hello, $name!"
echo "$greeting"

# Curly brace expansion
echo "${name}Script"

# Reassignment
name="Bash"
echo "$name"

# Unquoted expansion
x=42
echo $x

# Multiple assignments on separate lines
a="first"
b="second"
c="third"
echo "$a $b $c"
