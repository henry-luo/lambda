// @expect-error: E312
// @description: function-type variance remains TGO14(b)

type Unary = fn(a: int) int
type Wider = fn(a: number) number
Unary <: Wider
