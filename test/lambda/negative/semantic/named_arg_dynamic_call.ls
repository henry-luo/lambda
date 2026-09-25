// @expect-error: E212
// @description: S12.3.2 / D6.2.2v2 -- a call through a function value has no
// declaration to bind names against, so its named arguments are rejected
// instead of being passed by position (LR07-16: this returned -4, not 4).
fn f(a, b) => a - b
let g = f
g(b: 1, a: 5)
