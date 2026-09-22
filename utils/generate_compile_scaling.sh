#!/bin/sh
set -eu

output_dir=${1:-temp/astscale}
mkdir -p "$output_dir"

for count in 250 500 1000 2000 4000 8000; do
    awk -v count="$count" 'BEGIN {
        print "let value_0 = 0"
        for (i = 1; i <= count; i++) {
            printf "let value_%d = value_%d + 1\n", i, i - 1
        }
        printf "value_%d\n", count
    }' >"$output_dir/lets_$count.ls"

    awk -v count="$count" 'BEGIN {
        print "fn chain_0(value) { value }"
        for (i = 1; i <= count; i++) {
            printf "fn chain_%d(value) { chain_%d(value) }\n", i, i - 1
        }
        printf "chain_%d(0)\n", count
    }' >"$output_dir/fns_$count.ls"
done
