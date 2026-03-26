#!/bin/bash
# Test file test operators: -f, -d, -e, -r, -w, -x, -s, -L

# create test files
echo "test content" > temp/test_file.txt

# --- -f (regular file) ---
echo "=== Test -f ==="
if [ -f temp/test_file.txt ]; then
    echo "regular file exists"
fi
if [ -f temp/nonexistent_file.txt ]; then
    echo "ERROR: should not exist"
else
    echo "nonexistent file not found"
fi

# --- -d (directory) ---
echo "=== Test -d ==="
if [ -d temp ]; then
    echo "directory exists"
fi
if [ -d temp/test_file.txt ]; then
    echo "ERROR: file is not directory"
else
    echo "file is not a directory"
fi

# --- -e (exists) ---
echo "=== Test -e ==="
if [ -e temp/test_file.txt ]; then
    echo "file exists"
fi
if [ -e temp ]; then
    echo "directory exists"
fi
if [ -e temp/nonexistent ]; then
    echo "ERROR: should not exist"
else
    echo "nonexistent not found"
fi

# --- -r (readable) ---
echo "=== Test -r ==="
if [ -r temp/test_file.txt ]; then
    echo "file is readable"
fi

# --- -w (writable) ---
echo "=== Test -w ==="
if [ -w temp/test_file.txt ]; then
    echo "file is writable"
fi

# --- -s (non-zero size) ---
echo "=== Test -s ==="
if [ -s temp/test_file.txt ]; then
    echo "file has content"
fi
echo -n "" > temp/empty_file.txt
if [ -s temp/empty_file.txt ]; then
    echo "ERROR: empty file should be zero size"
else
    echo "empty file has no content"
fi

# --- -f with [[ ]] ---
echo "=== Test [[ -f ]] ==="
if [[ -f temp/test_file.txt ]]; then
    echo "extended test: file exists"
fi
if [[ -f temp/no_such_file ]]; then
    echo "ERROR"
else
    echo "extended test: file not found"
fi

# --- negation ---
echo "=== Test negation ==="
if [ ! -f temp/nonexistent ]; then
    echo "negated: nonexistent confirmed"
fi
if [ ! -d temp ]; then
    echo "ERROR"
else
    echo "negated: temp is a directory"
fi

echo "=== Done ==="
