// S9.1.2/D5.3: activation-local payloads never escape or change caller values.
pn visit(row: int, marks, limit: int) {
    if (row == limit) { return 0 }
    marks[row] = row + 1
    let value = marks[row]
    return value + visit(row + 1, marks, limit)
}
pn escape(marks) { marks[0] = 9; return marks }
pn range_result(marks) { marks[0] = 7; return marks[0 to 1] }
pn retained(marks) { let old = marks; marks[0] = 7; return old[0] }
pn selected(marks, key) { marks[0] = 7; return marks[key] }
pn recursive_select(key, marks, stop: bool) {
    marks[0] = 7
    if (stop) { return marks[key] }
    return recursive_select(key, marks, true)
}
pn through_function(fn_value, marks, key) { return fn_value(key, marks, true) }
pn main() {
    let small = fill(5, 0)
    let large = fill(80, 0)
    print([visit(0, small, 5), small, visit(0, large, 3), large[0]]); print("\n")
    let escaped = escape(small)
    let ranged = range_result(small)
    print([escaped, ranged, retained(small), selected(small, 0), small]); print("\n")
    print(selected(small, 0 to 1)); print("\n")
    let floats = fill(5, 0.5)
    print([visit(0, floats, 3), floats]); print("\n")
    print([recursive_select(0, small, false),
        through_function(recursive_select, small, 0 to 1), small]); print("\n")
}
