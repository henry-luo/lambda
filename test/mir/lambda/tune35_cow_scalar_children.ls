// D3.4.6, S9.1.2: scalar lanes need no sharing mark; every container lane still does.
type Child = {value: int}
type Record = {number: int, real: float, wide: i64, maybe: float?, flag: bool,
    text: string, nums: int[], optional: Child?, dynamic: any}

pn main() {
    var root: Record = {number: 1, real: -0.0, wide: 9007199254740993i64, maybe: null,
        flag: true, text: "scalar", nums: [1, 2], optional: {value: 3}, dynamic: {items: [4, 5]}}
    let before = root
    root.number = 2
    root.nums[0] = 99
    root.optional.value = 9
    root.dynamic.items[0] = 80
    print([before.number == 1, root.number == 2, before.nums == [1, 2], root.nums == [99, 2],
        before.optional.value == 3, root.optional.value == 9,
        before.dynamic.items == [4, 5], root.dynamic.items == [80, 5],
        root.wide == before.wide, root.maybe == null, root.text == "scalar", root.flag]); print("\n")
}
