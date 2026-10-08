// Test: Wrong Argument Count with an optional parameter
// Layer: 2 | Category: negative | Covers: E206 arity range (S12.3.6)
// An optional parameter makes the accepted arity a RANGE; the diagnostic must
// report both ends, not just the required count.

fn g(a, b?) => if (b) a + b else a

// Too many — accepted arity is 1..2
g(1, 2, 3)
