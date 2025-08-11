// More specific test cases for conditional type coercion edge cases

// These should work (homogeneous types)
(if (true) "yes" else "no")
(if (1 > 0) 42 else 0)

// These are problematic (heterogeneous types) 
(if (false) null else "default")
(if (true) 123 else "text")
(if (false) "string" else 456)
