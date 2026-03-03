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
    
    # Check if file uses procedural mode (needs "lambda.exe run")
    if head -5 "$f" | grep -q "// Mode: procedural"; then
        out=$(./lambda.exe run "$f" 2>/dev/null)
    else
        out=$(./lambda.exe "$f" 2>/dev/null)
    fi
    rc=$?
    
    if [ $rc -ne 0 ]; then
        echo "FAIL (exit $rc): $f"
        # Show error
        if head -5 "$f" | grep -q "// Mode: procedural"; then
            ./lambda.exe run "$f" 2>&1 | head -5
        else
            ./lambda.exe "$f" 2>&1 | head -5
        fi
        echo "---"
    else
        echo "$out" > "$expected"
        echo "OK: $f -> $expected"
    fi
done

echo "Done."
