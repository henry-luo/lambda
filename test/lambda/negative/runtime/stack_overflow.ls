// @expect-error: E308
// @description: Stack overflow from infinite recursion (non-tail-recursive to defeat TCO)

fn f(n) => n + f(n + 1)
let x = f(0)
