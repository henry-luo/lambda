// @expect-error: E206
// @description: LR07-19 -- a method's named arguments cannot leave out a
// required parameter either; the call had bound its arguments by position.
type T { k: int, fn q(a, b, c = 0) => [a, b, c, k] }
let t = <T k: 1>
t.q(a: 1, c: 3)
