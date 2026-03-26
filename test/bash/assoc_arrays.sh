#!/bin/bash
# Associative array tests

# Declare and populate
declare -A colors
colors[red]="#ff0000"
colors[green]="#00ff00"
colors[blue]="#0000ff"

# Access values
echo "${colors[red]}"
echo "${colors[green]}"
echo "${colors[blue]}"

# Count elements
echo "${#colors[@]}"

# Overwrite a value
colors[red]="#FF0000"
echo "${colors[red]}"

# Unset a key
unset 'colors[green]'
echo "${#colors[@]}"

# Non-existent key returns empty string
result="${colors[yellow]}"
echo "empty=${result}"

# Iterate over values
declare -A scores
scores[alice]=95
scores[bob]=87
scores[charlie]=92
total=0
for val in "${scores[@]}"; do
    total=$(( total + val ))
done
echo "$total"

# Iterate over keys
declare -A fruits
fruits[apple]=red
fruits[banana]=yellow
fruits[grape]=purple
count=0
for key in "${!fruits[@]}"; do
    count=$(( count + 1 ))
done
echo "$count"
