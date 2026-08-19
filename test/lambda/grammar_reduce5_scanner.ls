// External-scanner coverage for static paths, declaration returns, and views.
fn union_ok() int | float { 1 }
fn nullable_ok() int? { 3 }
fn raised_ok() int^error { 2 }
fn division_ok() float { 1.0 / float(2) }
fn pipe_ok() string {
    let parts = ["a", "b"]
    parts |> join("")
}

view int | string { ~ }
view named_element: <item> { ~ }

[/.root.branch, union_ok(), nullable_ok(), raised_ok() or 0, division_ok(), pipe_ok()]
