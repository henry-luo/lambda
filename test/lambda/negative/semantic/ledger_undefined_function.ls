// S16.10.4: unresolved bare calls are rejected at compile time.

// Call undefined function - should produce error E203
nonexistent_function(42)

// Misspelled function
pritn("hello")

// Call with wrong name
fn greet(name: string) => "Hello, " ++ name
greeet("world")
