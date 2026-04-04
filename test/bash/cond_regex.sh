#!/bin/bash
# Conditional engine tests: [[ =~ ]] with BASH_REMATCH

# Basic regex match
str="hello world 123"
if [[ $str =~ ([0-9]+) ]]; then
    echo "match: ${BASH_REMATCH[0]}"
    echo "group1: ${BASH_REMATCH[1]}"
else
    echo "no match"
fi

# Multiple capture groups
line="2024-01-15 Jane Doe"
if [[ $line =~ ^([0-9]{4})-([0-9]{2})-([0-9]{2})\ (.+)$ ]]; then
    echo "year: ${BASH_REMATCH[1]}"
    echo "month: ${BASH_REMATCH[2]}"
    echo "day: ${BASH_REMATCH[3]}"
    echo "name: ${BASH_REMATCH[4]}"
fi

# No match case
if [[ "abc" =~ ^[0-9]+$ ]]; then
    echo "digits"
else
    echo "not digits"
fi

# BASH_REMATCH count
email="user@example.com"
if [[ $email =~ ^([^@]+)@(.+)$ ]]; then
    echo "parts: ${#BASH_REMATCH[@]}"
    echo "user: ${BASH_REMATCH[1]}"
    echo "domain: ${BASH_REMATCH[2]}"
fi

# Pattern matching with glob
str="hello.txt"
if [[ $str == *.txt ]]; then
    echo "is txt file"
fi

if [[ $str == *.pdf ]]; then
    echo "is pdf file"
else
    echo "not pdf file"
fi
