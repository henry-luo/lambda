// S11.2.1 / S11.4.1v3 (LR03-11): a literal type is the singleton of its value,
// so `1 | 2` admits 1 and 2 only. Both tiers admitted any int by its TypeId.
fn dyn(v) => v
fn g(x: 1 | 2) { x }
"bound: " ++ string(g(dyn(3)))
