// @expect-error: E205
// @description: Duplicate parameter names in function

let f = (x, x) => x + 1
