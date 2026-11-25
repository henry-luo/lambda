#!/bin/bash
set -o pipefail
cut -d, -f2 input.csv | sort | uniq -c
