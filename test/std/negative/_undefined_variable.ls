// Test: Undefined Variable
// Layer: 2 | Category: negative | Covers: reference undefined name

// Reference undefined variable - should produce error E202
undefined_var

// Use undefined in expression
1 + unknown_name

// Undefined in function body
fn test() => nonexistent_var
test()
