// S11.2.1 (LR03-31): `true` in type position is the singleton {true}; it had no
// value, so it admitted `false` as well.
let src = [true, false]
let c: true = src[1]
"bound: " ++ string(c)
