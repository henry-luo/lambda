// S16.10.4: each unbound value name is rejected before execution.

// Reference undefined variable - should produce error E202
undefined_var

// Use undefined in expression
1 + unknown_name

// Undefined in function body
fn test() => nonexistent_var
test()
