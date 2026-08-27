// S12.3.7: a non-callable shadow is the ordinary not-callable error — never a
// silent fallback to the builtin. This returned the builtin's 3 before the fix.
let sum = 5
sum([1, 2])
