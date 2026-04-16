// Infinite tail recursion - should trigger TCO iteration limit
// TCO converts this to a goto loop; the counter guard should catch it

fn infinite_recurse(n) => infinite_recurse(n + 1)
infinite_recurse(0)
