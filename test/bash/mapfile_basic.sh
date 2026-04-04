#!/bin/bash
# mapfile / readarray tests

# Basic mapfile from here-string
mapfile -t lines <<< "alpha
beta
gamma"
echo "count: ${#lines[@]}"
echo "first: ${lines[0]}"
echo "second: ${lines[1]}"
echo "third: ${lines[2]}"

# readarray (synonym for mapfile)
readarray -t items <<< "one
two
three"
echo "items: ${#items[@]}"
echo "item0: ${items[0]}"
echo "item2: ${items[2]}"

# mapfile with -n (max count)
mapfile -t -n 2 partial <<< "a
b
c
d"
echo "partial count: ${#partial[@]}"
echo "partial: ${partial[0]} ${partial[1]}"

# mapfile with -s (skip lines)
mapfile -t -s 1 skipped <<< "skip_me
keep_this
and_this"
echo "skipped: ${skipped[0]}"
echo "skipped: ${skipped[1]}"
