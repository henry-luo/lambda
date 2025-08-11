// Test the specific ternary expression patterns mentioned in the issue
// These were the ones causing "incompatible types in true and false parts of cond-expression"

// Test case 1: For loop with heterogeneous array and conditional expression
(for (item in [1, null, 3]) (if (item) item else "empty"))

// Test case 2: Another problematic pattern  
(for (val in [null, true, false, 123]) (if (val) val else "none"))

// Test case 3: Direct conditional with mixed types
(if (true) null else "default")
(if (false) 123 else "text")
(if (true) "string" else 456)
