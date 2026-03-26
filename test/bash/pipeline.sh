#!/bin/bash
# Pipeline tests: builtin-to-builtin data passing

# Basic: echo | cat
echo "hello" | cat

# Multi-stage: echo | cat | cat
echo "world" | cat | cat

# echo | wc -c (character count)
echo -n "abcdef" | wc -c

# echo | wc -w (word count)
echo "one two three four" | wc -w

# echo | wc -l (line count)
printf "line1\nline2\nline3\n" | wc -l

# echo | head -n 2
printf "alpha\nbeta\ngamma\ndelta\n" | head -n 2

# echo | tail -n 2
printf "alpha\nbeta\ngamma\ndelta\n" | tail -n 2

# echo | grep pattern
printf "apple\nbanana\napricot\ncherry\n" | grep "ap"

# echo | grep -v (invert)
printf "apple\nbanana\napricot\ncherry\n" | grep -v "ap"

# echo | grep -c (count)
printf "apple\nbanana\napricot\ncherry\n" | grep -c "a"

# echo | sort
printf "cherry\napple\nbanana\n" | sort

# echo | sort -r (reverse)
printf "cherry\napple\nbanana\n" | sort -r

# echo | sort -n (numeric)
printf "10\n2\n30\n1\n" | sort -n

# echo | tr (transliterate)
echo "hello" | tr "el" "ip"

# echo | tr -d (delete)
echo "hello world" | tr -d "lo"

# echo | cut -d ' ' -f 2
echo "one two three" | cut -d ' ' -f 2

# Multi-stage pipeline: echo | grep | wc -l
printf "apple\nbanana\napricot\ncherry\n" | grep "a" | wc -l

# Multi-stage: echo | sort | head -n 2
printf "cherry\napple\nbanana\ndelta\n" | sort | head -n 2

# Negated pipeline
! echo "hello" | grep -q "xyz"
echo $?

# Pipeline exit code (grep finds match = 0)
echo "hello" | grep -q "hello"
echo $?

# Pipeline exit code (grep no match = 1)
echo "hello" | grep -q "xyz"
echo $?
