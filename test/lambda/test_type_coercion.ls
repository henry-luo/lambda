// Test case to isolate type coercion issues in conditional expressions

// Simple case that should work (homogeneous types)
(for (item in [1, 2, 3]) (if (item) item else 0))

// Problematic case (heterogeneous array with conditional returning different types)
(for (item in [1, null, 3]) (if (item) item else "empty"))

// Another problematic case  
(for (val in [null, true, false, 123]) (if (val) val else "none"))
