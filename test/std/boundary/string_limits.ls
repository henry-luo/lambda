// Test: String Limits
// Layer: 2 | Category: boundary | Covers: empty string, long strings, special chars

// Empty string behavior
type("")
"" == null
len("a")

// String with special characters
"hello\nworld"
"tab\there"
"quote\"inside"
len("abc")

// Unicode
"café"
len("café")
"日本語"
