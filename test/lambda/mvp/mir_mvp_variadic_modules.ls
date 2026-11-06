// D7.2.2/S16.9.7: imports and bound methods use the same trailing rest ABI.
import source: ._mir_mvp_variadic_module
type Reader {
    value: int,
    fn collect(head = 7, ...) => [value + head, varg()]
}
let reader = <Reader value: 10>
let imported = source.collect
let bound = reader.collect;
[
    source.collect() == [0, []],
    source.collect(1, "a", "b", null) == [1, ["a", "b", null]],
    imported(2, (3, 4)) == [2, [[3, 4]]],
    reader.collect() == [17, []],
    reader.collect(2, "x", null) == [12, ["x", null]],
    bound(3, [4, 5]) == [13, [[4, 5]]]
]
