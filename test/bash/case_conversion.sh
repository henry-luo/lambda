#!/bin/bash
# Case conversion tests (Bash 4.0+ features)

# Uppercase (first character)
word="lambda"
echo "${word^}"

# Uppercase (all characters)
echo "${word^^}"

# Lowercase (first character)
upper="LAMBDA"
echo "${upper,}"

# Lowercase (all characters)
echo "${upper,,}"

# Mixed case
mixed="hElLo WoRlD"
echo "${mixed^^}"
echo "${mixed,,}"
