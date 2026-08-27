// External-scanner coverage for static paths, declaration returns, and views.
fn union_ok() int | float { 1 }
fn nullable_ok() int? { 3 }
fn raised_ok() int^error { 2 }
fn division_ok() float { 1.0 / float(2) }
fn leading_dot_float_ok() float { .123 }
fn member_ok() int {
    let st = {moves: 7}
    st.moves
}
fn pipe_ok() string {
    let parts = ["a", "b"]
    parts |> join("")
}

view int | string { ~ }
view named_element: <item> { ~ }

[/.root.branch, \.relative.branch, union_ok(), nullable_ok(), raised_ok() or 0,
 division_ok(), leading_dot_float_ok(), member_ok(), pipe_ok()]
