#!/bin/bash
# Test file redirections

# --- stdout redirect: > (write) ---
echo "=== Write redirect ==="
echo "hello world" > temp/redir_test.txt
echo "File written"

# read it back with < redirect
cat < temp/redir_test.txt

# --- append redirect: >> ---
echo "=== Append redirect ==="
echo "line one" > temp/redir_append.txt
echo "line two" >> temp/redir_append.txt
echo "line three" >> temp/redir_append.txt
cat < temp/redir_append.txt

# --- overwrite with > ---
echo "=== Overwrite redirect ==="
echo "original" > temp/redir_overwrite.txt
echo "replaced" > temp/redir_overwrite.txt
cat < temp/redir_overwrite.txt

# --- redirect to /dev/null (suppress stdout) ---
echo "=== Suppress stdout ==="
echo "this should not appear" > /dev/null
echo "after suppress"

# --- stderr redirect (2>/dev/null) ---
echo "=== Stderr redirect ==="
echo "visible output" 2>/dev/null
echo "after stderr redirect"

# --- multiple lines to file ---
echo "=== Multi-line file ==="
echo "apple" > temp/redir_multi.txt
echo "banana" >> temp/redir_multi.txt
echo "cherry" >> temp/redir_multi.txt
cat < temp/redir_multi.txt

# --- read redirect into pipeline builtins ---
echo "=== Read into wc ==="
echo "one two three" > temp/redir_wc.txt
wc -w < temp/redir_wc.txt

# --- empty file ---
echo "=== Empty file ==="
echo -n "" > temp/redir_empty.txt
cat < temp/redir_empty.txt
echo "after empty"

# cleanup
echo "=== Done ==="
