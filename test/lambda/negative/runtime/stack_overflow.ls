// @expect-error: E304
// @description: Stack overflow from infinite recursion

let f = (n) => f(n + 1)
let x = f(0)
