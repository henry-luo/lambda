// S11.4.10 (LR03-20): a field value the compiler cannot prove is checked at
// construction. Unchecked, the string's pointer was read back from the int
// field.
type Obj { a: int }
fn dyn(v) => v
"bound: " ++ string(<Obj a: dyn("x")>)
