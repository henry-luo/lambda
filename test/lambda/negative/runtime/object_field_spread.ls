// S11.4.10 (LR03-20): fields a spread supplies are admitted like written ones.
type Pair { a: int, b: string }
let src = {a: "no", b: "four"}
"bound: " ++ string(<Pair *: src>)
