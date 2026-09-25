// @expect-error: E100
// @description: S10.1.6: `|:` lexes by longest match, so a match arm that
// glues a union bar to the arm colon is the filter token, not `| :`. A union
// with no right operand was never valid, so the glued form stays an error.

let v = 3
match v {
    case int |: "int"
    default: "other"
}
