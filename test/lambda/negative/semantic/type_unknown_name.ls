// S11.4.12: a type name that names nothing is E204; it had read as `any`, so
// the annotation checked nothing.
fn f(x: Nope) => x
f(1)
