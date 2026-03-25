#!/bin/bash
# While and until loop tests

# Basic while loop
i=0
while [ $i -lt 5 ]; do
    echo "$i"
    i=$(( i + 1 ))
done

# While with accumulator
sum=0
n=1
while [ $n -le 10 ]; do
    sum=$(( sum + n ))
    n=$(( n + 1 ))
done
echo "sum: $sum"

# Until loop (opposite of while)
count=5
until [ $count -le 0 ]; do
    echo "countdown: $count"
    count=$(( count - 1 ))
done

# Break in while
j=0
while true; do
    if [ $j -ge 3 ]; then
        break
    fi
    echo "j=$j"
    j=$(( j + 1 ))
done

# Continue in loop
for k in 1 2 3 4 5; do
    if [ $k -eq 3 ]; then
        continue
    fi
    echo "k=$k"
done
