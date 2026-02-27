// Infinite non-tail recursion - should trigger stack overflow signal
// n + f(n+1) is not a tail call, so stack grows until overflow

fn f(n) => n + f(n + 1)
let x = f(0)
