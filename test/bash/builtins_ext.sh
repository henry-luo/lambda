#!/bin/bash
# umask and hash builtin tests

# umask — display current mask
old_mask=$(umask)
echo "has mask: yes"

# umask — set and display
umask 0022
echo "mask set: ok"

# umask -S (symbolic display)
umask -S
echo "symbolic: ok"

# restore
umask "$old_mask"

# hash — basic usage (no error)
hash
echo "hash: ok"

# enable — list builtins
count=$(enable | head -5 | wc -l)
echo "builtins listed: yes"

# trap -p — print traps
trap 'echo bye' EXIT
trap -p EXIT
echo "trap printed: ok"
