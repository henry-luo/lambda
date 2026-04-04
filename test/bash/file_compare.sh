#!/bin/bash
# File comparison tests: -nt, -ot, -ef

# Create two temp files with different timestamps
tmpdir="./temp/bash_file_cmp_test"
mkdir -p "$tmpdir"
echo "older" > "$tmpdir/old_file.txt"
sleep 1
echo "newer" > "$tmpdir/new_file.txt"

# -nt (newer than)
if [[ "$tmpdir/new_file.txt" -nt "$tmpdir/old_file.txt" ]]; then
    echo "new_file is newer"
else
    echo "unexpected: new_file is not newer"
fi

# -ot (older than)
if [[ "$tmpdir/old_file.txt" -ot "$tmpdir/new_file.txt" ]]; then
    echo "old_file is older"
else
    echo "unexpected: old_file is not older"
fi

# -ef (same file via hardlink)
ln "$tmpdir/old_file.txt" "$tmpdir/old_link.txt" 2>/dev/null
if [[ "$tmpdir/old_file.txt" -ef "$tmpdir/old_link.txt" ]]; then
    echo "same file"
else
    echo "not same file"
fi

# different files are not -ef
if [[ "$tmpdir/old_file.txt" -ef "$tmpdir/new_file.txt" ]]; then
    echo "unexpected: different files are ef"
else
    echo "different files"
fi

# nonexistent file
if [[ "$tmpdir/nonexistent" -nt "$tmpdir/old_file.txt" ]]; then
    echo "unexpected"
else
    echo "nonexistent not newer"
fi

# cleanup
rm -rf "$tmpdir"
