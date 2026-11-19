// S7.9.3/S8.3.3v3: generic value operators carry the original error Item.
fn wrapped() any^ => error("kept")
let problem = wrapped();
[(problem in [1]).message, (1 in problem).message,
 (problem at {x: 1}).message, (problem of [1]).message, count(problem).message,
 1 in [1], "x" at {x: 1}, 0 of [1], count(null), count([1, 2]), count((1, 2))]
