#!/bin/bash
set -o pipefail
cut -d, -f1,2 input.csv | awk -F, '$1 == "ERROR" { print toupper($2) }' | cat
