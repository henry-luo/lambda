// LR02-5: has_current_item_ref walked a match node's arm list without
// inspecting anything — the loop body was empty and it fell through to false.
// A pipe therefore never established the current-item context an arm needed,
// and `xs |> match (1) { case int: (~) * 10 }` evaluated to `error`.
//
// Per doc/Lambda_Expr_Stam.md, `~` inside an arm body is the MATCHED VALUE, so
// the arm rebinds it and the arm PATTERN is deliberately not walked — a
// `that` constraint's `~` is the match subject too, the same shadowing the
// handler case models.
let xs = [1, 2, 3]

let out = {
    // the scrutinee carries `~`, so the arm sees each piped item in turn
    subject_is_item: xs |> match (~) { case int: (~) * 10
                                       default: 0 },
    // the scrutinee is a constant, so the arm's `~` is that constant for every
    // piped item — this is the shape that used to fail outright
    subject_is_const: xs |> match (1) { case int: (~) * 10
                                        default: 0 },
    // an arm body with no reference at all
    no_ref: xs |> match (~) { case int: 7
                              default: 0 },
    // a `that` constraint's `~` is the match subject, not the pipe item
    with_that: xs |> match (~) { case int that (~ > 1): "big"
                                 default: "small" },
    // and without a pipe, `~` is still the matched value
    plain: match (5) { case int: (~) * 10
                       default: 0 }
}
out
