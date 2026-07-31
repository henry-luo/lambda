// @expect-error: E312
// `or` is a runtime recovery operator; `|` forms a type union.
let bad = int or string
