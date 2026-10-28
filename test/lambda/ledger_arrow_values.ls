// S16.6.7v2: anonymous functions are values, named definitions are declarations.
let f = { let c = 10; (x) => c + x }
let pair = (1, (x) => x * 2)
let named = { fn g(x) => x + 1; g };
[f(2), len(pair), pair[1](4), named(8)]
