// S7.7.2v2/S11.4.4: a field read cannot hide its receiver's validation error.
type Box = {n: int}
fn opaque(value) => value
let box: Box = opaque("wrong")
let value: int = box.n
value
