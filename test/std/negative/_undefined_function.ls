// Test: Undefined Function
// Layer: 2 | Category: negative | Covers: call non-existent function

// Call undefined function - should produce error E203
nonexistent_function(42)

// Misspelled function
pritn("hello")

// Call with wrong name
fn greet(name: string) => "Hello, " & name
greeet("world")
