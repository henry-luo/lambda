#!/bin/bash
# Generate .expected files from .ls test files by running them through lambda.exe
# Usage: bash test/std/generate_expected.sh

cd "$(dirname "$0")/../.." || exit 1

echo "Generating expected outputs for test/std/ tests..."

total=0
passed=0
failed=0

find test/std -name "*.ls" | sort | while read -r f; do
    base="${f%.ls}"
    expected="${base}.expected"
    
    out=$(./lambda.exe "$f" 2>/dev/null)
    rc=$?
    
    if [ $rc -ne 0 ]; then
        echo "FAIL (exit $rc): $f"
        # Show error
        ./lambda.exe "$f" 2>&1 | head -5
        echo "---"
    else
        echo "$out" > "$expected"
        echo "OK: $f -> $expected"
    fi
done

echo "Done."
