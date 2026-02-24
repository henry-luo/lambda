// @expect-error: E100
// @description: '...' spread syntax is not valid in Lambda function calls

let a = [1, 2, 3]
fn f(x, y) => x + y
f(...a)
