// MIR public wrappers must forward subnormal results through the caller home.
fn tiny() float => 5e-324
fn apply(callback) => callback()

let result = apply(tiny)
[result, result == 5e-324, apply(tiny) == result]
