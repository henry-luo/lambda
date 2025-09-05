// Test: String Basic Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// String literals
let single_quoted = 'Hello, World!'
let double_quoted = "Hello, World!"
let multi_line = """
  This is a
  multi-line
  string.
"""

// String concatenation
let hello = "Hello, "
let world = "World!"
let greeting = hello + world  // "Hello, World!"

// String length
let len = greeting.length()  // 13

// String indexing
let first_char = greeting[0]    // 'H'
let last_char = greeting[-1]    // '!'
let substring = greeting[0:5]   // "Hello"

// String methods
let upper = greeting.upper()    // "HELLO, WORLD!"
let lower = greeting.lower()    // "hello, world!"
let trimmed = "  hello  ".trim()  // "hello"

// String interpolation
let name = "Alice"
let message = `Hello, ${name}!`  // "Hello, Alice!"

// Final check
1
