// S9.1.1/S9.1.3: a pipe receiver cannot borrow an immutable binding for mutation.
pn mutate(var value: array) { push(value, 2) }
pn main() {
    let value = [1]
    value |> mutate()
}
