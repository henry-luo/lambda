// MIR public wrappers must forward subnormal results through the caller home.
fn tiny() float => 5e-324
fn apply_fn(callback) => callback()

let result = apply_fn(tiny);
[result, result == 5e-324, apply_fn(tiny) == result]
