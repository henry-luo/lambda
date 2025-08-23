// Test: String Negative Cases
// Category: core/datatypes
// Type: negative
// Expected: error

// Invalid string operations that should fail
"hello" + 42
"world" - "test"
"abc" * "def"
"hello" / 2

// Invalid string indexing
let str = "hello"
str[10]  // Index out of bounds
str[-1]  // Negative index

// Invalid method calls
str.invalidMethod()
