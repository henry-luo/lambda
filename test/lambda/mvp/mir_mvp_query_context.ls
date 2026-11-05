// S2.5.5v2: demanded content queries collect even when parsed in statement form.
fn sequence(lower: any, upper: any) { [for (value in (lower to upper)^) value] }
pn total(bound) {
    var result = 0
    for value in 1 to bound { result = result + value }
    return result
}

pn main() {
    print(total(4)); print("\n")
    print(sequence(2, 4)); print("\n")
    print(sequence("a", "c")); print("\n")
    let children = <rows for value in [1, 2, 3] { <row value> }>
    print([len(content(children)), children[0][0], children[1][0], children[2][0]]); print("\n")
    // datetime constructors return packed C bits, while source values carry owned pointers.
    print([date(2025, 4, 26) == t'2025-04-26', time(10, 30, 45) == t'10:30:45',
        date(2025, 4, 26) is date, time(10, 30, 45) is time]); print("\n")
}
