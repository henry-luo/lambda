#!/bin/bash
# word_expand.sh — Integration tests for word expansion (Module 1)

# ===== IFS word splitting =====

# Test 1: Default IFS splitting (space/tab/newline)
str="hello   world	tab"
set -- $str
echo "default IFS: $# words: $1 $2 $3"

# Test 2: Custom IFS
IFS=":"
str="one:two:three"
set -- $str
echo "colon IFS: $# words: $1 $2 $3"

# Test 3: IFS with empty fields (non-whitespace delimiter preserves empties)
IFS=":"
str="a::b"
set -- $str
echo "empty field: $# words: '$1' '$2' '$3'"

# Test 4: IFS with leading/trailing whitespace
IFS=" "
str="  hello  world  "
set -- $str
echo "trimmed: $# words: $1 $2"

# Test 5: Empty IFS (no splitting)
IFS=""
str="hello world"
set -- $str
echo "empty IFS: $# words: '$1'"

# Test 6: IFS with mixed whitespace and non-whitespace
IFS=": "
str="one : two : three"
set -- $str
echo "mixed IFS: $# words: $1 $2 $3"

# Reset IFS
unset IFS

# ===== Quote removal =====

# Test 7: Single quotes
echo 'hello world'

# Test 8: Double quotes
echo "hello world"

# Test 9: Backslash escape
echo hello\ world

# ===== ANSI-C escapes ($'...') =====

# Test 10: Basic escapes
echo $'tab:\there'
echo $'newline:\nhere'

# Test 11: Hex and octal
echo $'\x41\x42\x43'
echo $'\101\102\103'

# Test 12: Unicode (bash 4+ feature)
echo $'\u0041\u0042\u0043'

# Test 13: Escaped backslash and quote
echo $'back\\slash'
echo $'single\'quote'
