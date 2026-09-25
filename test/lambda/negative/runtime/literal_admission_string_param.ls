// S11.2.1 / S11.4.1v3 (LR03-11): the static relation proved a `string`
// argument against the literal contract `"a"` by TypeId, so the JIT dropped the
// runtime check and bound "c"; the interpreter rejected it.
fn dyn(v) => v
fn h(x: "a") { x }
"bound: " ++ h(dyn("c"))
