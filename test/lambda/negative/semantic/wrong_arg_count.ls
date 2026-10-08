// Test: Wrong Argument Count
// Layer: 2 | Category: negative | Covers: too few/many args to typed function

fn add(a: int, b: int) => a + b

// Too few arguments - should produce error E206
add(1)

// Too many arguments
add(1, 2, 3)

// Zero arguments
add()
