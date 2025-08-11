# Test various malformed syntax patterns that should produce clear error messages
# instead of crashing

# Range syntax error
print (1..3)

# Other potential issues
let x = 1..5
for i in 1..10:
    print i

# Field access after range
let y = 1..3.length
