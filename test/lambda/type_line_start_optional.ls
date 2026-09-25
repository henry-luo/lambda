// S16.2.2v2 (LR02-26): `?` can only continue an expression, so a line-start
// `?` after a complete type continues the type, in an alias, an annotation and
// a parameter list alike (S16.2.1). The C parser ended the type at the break.
type MaybeInt = int
?
let x: int
? = null
fn f(a: int
?) { a }
let r = [null is MaybeInt, 5 is MaybeInt, "s" is MaybeInt, x, f(null), f(7)]
r
