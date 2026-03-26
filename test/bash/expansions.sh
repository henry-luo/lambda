#!/bin/bash
# Phase 5: Expansions & Word Processing

# === Tilde Expansion ===
# Test that ~ produces a non-empty string
result=$(echo ~)
if [ -n "$result" ]; then
  echo "tilde: expanded"
else
  echo "tilde: empty"
fi

# Test ~/path appends to tilde result  
result2=$(echo ~/mypath)
if [ -n "$result2" ]; then
  echo "tilde-path: expanded"
else
  echo "tilde-path: empty"
fi

# === Brace Expansion: comma lists ===
echo {a,b,c}
echo {hello,world}
echo {1,2,3,4,5}

# === Brace Expansion: numeric ranges ===
echo {1..5}
echo {3..1}

# === Brace Expansion: character ranges ===
echo {a..e}
echo {z..v}

# === Arithmetic Short-Circuit ===
# && : 0 && anything = 0 (short-circuit)
echo $(( 0 && 999 ))
# && : nonzero && nonzero = 1
echo $(( 5 && 3 ))
# && : nonzero && 0 = 0
echo $(( 1 && 0 ))

# || : nonzero || anything = 1 (short-circuit)
echo $(( 7 || 0 ))
# || : 0 || nonzero = 1
echo $(( 0 || 5 ))
# || : 0 || 0 = 0
echo $(( 0 || 0 ))

# Combined
echo $(( (1 && 1) || 0 ))
echo $(( (0 || 0) && 1 ))
