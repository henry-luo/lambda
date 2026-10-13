// Closures snapshot their captures by value (D6.2.3) on every tier.
// - A closure made in a comprehension at module level captures that iteration's
//   loop variable; the T0 interpreter read the variable's module slot, which holds
//   the last value once the comprehension ends.
// - A parenthesised let-list whose value is a function literal yields the function;
//   T0 set the literal apart as a declaration and returned null.

let module_level = [for (k in ["a", "b", "c"]) (x) => k ++ x]
"module-level comprehension:"; [for (f in module_level) f("1")]

fn in_function() => [for (k in ["a", "b"]) (x) => k ++ x]
"function-level comprehension:"; [for (f in in_function()) f("1")]

let table = map([for (k in ["p", "q"], x in [k, (y) => k ++ y]) x])
"map of closures:"; [table.p("?"), table.q("?")]

let adder = (let a = 1, (x) => x + a)
"let-list function:"; [adder(1)]

let lets = [for (k in ["a", "b"]) (let kk = k ++ k, (x) => kk ++ x)]
"let-list closures in a comprehension:"; [for (f in lets) f("!")]
