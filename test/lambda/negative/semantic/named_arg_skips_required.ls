// @expect-error: E206
// @description: LR07-19 -- named arguments cannot leave out a required
// parameter. The arity count held when a named optional stood in for it, and
// the tiers split: T0 failed the parameter's null and the JIT bound a zero.
fn h(a, b: int, c = 0) => [a, b, c]
h(a: 1, c: 3)
