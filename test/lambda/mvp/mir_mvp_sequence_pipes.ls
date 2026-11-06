// S10.1.2v4/S10.1.6: mapping and filtering share the canonical keyed walk.
fn retain(value: any) any {
    let pressure = [value, value, value]
    return value
}
fn no_key(value: int) bool { return ~key is null }
pn main() {
    print([1, 2, 3] |> ~ * 2); print("\n")
    print((1, 2, 3) |: ~ > 1); print("\n")
    print([5 |> ~ + 1, 5 |: ~ > 9, null |> ~, [] |> ~]); print("\n")
    let keyed = {a: 2, b: 4} |> [~key, ~]
    print([keyed[0][0] == 'a', keyed[0][1] == 2,
        keyed[1][0] == 'b', keyed[1][1] == 4]); print("\n")
    let source_row = <row count: 7, "child" {9}>
    let entries = source_row |> [~key, ~]
    print([len(entries) == 3, entries[0][0] == 'count', entries[0][1] == 7,
        entries[1][0] == 0, entries[1][1] == "child",
        entries[2][0] == 1, entries[2][1] == 9]); print("\n")
    print([("hé" |> ~) == "hé", ('xy' |> ~) == 'xy',
        ("hé" |: ~ != "x") == "hé"]); print("\n")
    let tree = [[1, 2], [3, 4]]
    let nested = tree |> [~key, (~ |> ~ * 10), ~key, ~./ == tree]
    print([nested[0][0] == 0, nested[0][1] == [10, 20], nested[0][2] == 0,
        nested[0][3], nested[1][0] == 1, nested[1][1] == [30, 40],
        nested[1][2] == 1, nested[1][3]]); print("\n")
    print([1, 2] |: (~ > 0 and error("predicate"))); print("\n")
    let walked = tree |> retain(~)
    print([walked == tree, tree == [[1, 2], [3, 4]]]); print("\n")
    print([~key is null, (7 that ~key == null) == 7]); print("\n")
    print([1, 2] |> no_key(~)); print("\n")
}
