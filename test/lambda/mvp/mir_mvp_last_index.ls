// S7.2.2: `last` belongs to the innermost subscript and counts content only.
pn source(var calls) { calls = calls + 1; return [10, 20, 30] }
pn increment(var value) { value = value + 1 }
pn main() {
    var calls = 0
    print([source(calls)[last], calls]); print("\n")
    print([[10, 20, 30][last - 1], [1, 2, 3][1 to last], [][last] is null]); print("\n")
    print([[1, 0, 2][[0, 1][last] + last - 1], [[10, 11], [20, 21]][last][last]]); print("\n")
    let row = <row attr: 7, *(1, 2, 3)>
    let empty_row = <row attr: 7>
    print(["aé"[last], row[last], empty_row[last] is null]); print("\n")
    var values = [1, 2, 3]
    values[last] = 9
    print(values); print("\n")
    var box = {values: [4, 5]}
    increment(box.values[last])^
    print(box.values); print("\n")
    var nested = [[1, 2], [3, 4]]
    nested[last][last] = 8
    print(nested); print("\n")
}
