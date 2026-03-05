// Test: String Manipulation
// Layer: 1 | Category: function | Covers: upper, lower, trim, starts_with, ends_with, contains

// ===== upper / lower =====
upper("hello")
lower("HELLO")
upper("Hello World")
lower("Hello World")

// ===== trim =====
trim("  hello  ")
trim_start("  hello  ")
trim_end("  hello  ")
trim("hello")

// ===== starts_with / ends_with =====
starts_with("hello", "hel")
starts_with("hello", "world")
ends_with("hello", "llo")
ends_with("hello", "hel")

// ===== contains =====
contains("hello world", "world")
contains("hello world", "xyz")
contains("hello", "")

// ===== index_of =====
index_of("hello world", "world")
index_of("hello world", "xyz")
index_of("hello", "l")

// ===== Method-style =====
"hello".upper()
"HELLO".lower()
"  hello  ".trim()
"hello".starts_with("hel")
"hello".ends_with("llo")
"hello world".contains("world")
