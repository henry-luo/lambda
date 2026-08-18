// IP1 fixtures [Type_Infer TIG5/TIG6/TIG11/TIG12/TIG16].
// Each binding pins a static type the AST dump asserts; the values are
// incidental — the point is that inference no longer falls back to `any`.

// TIG5: `and` yields the normalized union of both operands, not `any`.
// Both constituents can reach the result (a falsy left is returned as-is).
let and_same = true and false
let and_mixed = true and 7

// TIG6: a magnitude comparison over a statically comparable pair is `bool`,
// including string/string and datetime/datetime — not only native numerics.
let cmp_num = 1 < 2
let cmp_str = "a" < "b"

// TIG11: a declaration block with one value item collapses to that value at
// runtime, so its static type is the value's type, not a container.
let block_single = (let inner = 41, inner + 1)

// TIG12: unary minus on a known non-numeric cannot silently be `any`.
let neg_num = -5

// TIG16: `|` with `~` publishes the mapped element type.
let piped = [1, 2, 3] |> ~ + 1

[and_same, and_mixed, cmp_num, cmp_str, block_single, neg_num, piped]
