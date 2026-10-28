// @expect-error: E206
fn f(a, b = 2) => [a, b]
f(a: 5, a: 1)
