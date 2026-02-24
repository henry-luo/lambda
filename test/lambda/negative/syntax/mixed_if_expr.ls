// @expect-error: E100
// @description: Cannot use braced block in else branch of if-expression

let x = if (true) 1 else { let y = 2; y }
