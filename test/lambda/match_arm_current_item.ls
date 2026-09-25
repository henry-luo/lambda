// `~` inside a match arm is the MATCHED VALUE (S11.2.1): the arm binds it, for
// its pattern and its body, and outside the arm the enclosing current item is
// back (S10.1.3, S10.1.7v2). So a `~` in an arm is never the enclosing pipe's,
// and it never makes that pipe a mapping.
//
// History: LR02-5 (2026-08-25) fixed an empty loop in has_current_item_ref by
// counting arm bodies, which made `xs |> match (1) { case int: (~) * 10 }` map
// to [10, 10, 10]. S10.1.7v2 (2026-09-25) reversed that: the arm's `~` is the
// constant 1, so the pipe body has no free `~` and is whole-value application
// of a non-callable value (S10.1.2v4). Counting arm bodies would also let a
// bare field name, which S10.1.7v2 reads as `~.name` in an arm, flip the pipe's
// mode by binding state.
let xs = [1, 2, 3]

let out = {
    // the scrutinee carries `~`, so the arm sees each piped item in turn
    subject_is_item: xs |> match (~) { case int: (~) * 10
                                       default: 0 },
    // the arm's `~` is the constant 1; the `~` after the match is the pipe
    // item again, and it is what makes the pipe a mapping
    outer_restored: xs |> (match (1) { case int: (~) * 10
                                       default: 0 }) + ~,
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
