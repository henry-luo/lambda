// S9.1.2/S9.1.3v4: the pipe receiver is the first plain actual.
pn change(var values: array) { values[0] = 9; return 0 }
pn observe(values: array, ignored = 0) { return values[0] }
pn main() {
    var values = [3]
    let old = values |> observe(change(values))
    var more = [3]
    let old_indirect = more |> (if (change(more) == 0) observe else observe)
    print([old, values[0], old_indirect, more[0]])
}
