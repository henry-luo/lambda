// @expect-error: E206
fn f(a, b = 2) => [a, b]
f(5, a: 1)
