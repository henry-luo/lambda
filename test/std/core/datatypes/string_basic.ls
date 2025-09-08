// Test: String Basic Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// String literals
let single_quoted = 'Hello, World!'
single_quoted
let double_quoted = "Hello, World!"
double_quoted
let multi_line = """
multi_line
  This is a
  multi-line
  string.
"""
let hello = "Hello, "
hello
let world = "World!"
world
let greeting = hello + world  
greeting
let len = greeting.length()  
len
let first_char = greeting[0]    
first_char
let last_char = greeting[-1]    
last_char
let substring = greeting[0:5]   
substring
let upper = greeting.upper()    
upper
let lower = greeting.lower()    
lower
let trimmed = "  hello  ".trim()  
trimmed
let name = "Alice"
name
let message = `Hello, ${name}!`  
message
1
