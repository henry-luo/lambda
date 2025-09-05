// Test: String Negative Cases
// Category: core/datatypes
// Type: negative
// Expected: error

// Invalid string operations that should fail
"hello" + 42
"world" - "test"
"abc" * "def"
"hello" / 2
let str = "hello"
str
str[10]  // Index out of bounds
str[-1]  // Negative index
str.invalidMethod()
