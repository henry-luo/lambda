#!/bin/bash
set -o pipefail
grep '^ERROR,' input.csv | cut -d, -f2 | tr '[:lower:]' '[:upper:]'
