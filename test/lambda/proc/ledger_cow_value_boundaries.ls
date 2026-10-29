// S9.1.2/S9.1.3: every copy is independent before a later mutation.
pn inspect(var x, snapshot) { x.size = 50; print(snapshot.size); print("\n") }
type Box { size: int }
pn inspect_typed(var x: Box, snapshot: Box) { x.size = 50; print(snapshot.size); print("\n") }
pn inspect_array(var x: int[], snapshot: int[]) { x[0] = 50; print(snapshot[0]); print("\n") }
pn inspect_array_loop(var x: int[], snapshot: int[]) {
    var i = 0
    while (i < len(x)) { x[i] = x[i] + 1; i = i + 1 }
    print([x, snapshot]); print("\n")
}
pn main() {
    var b = {size: 5}
    inspect(b, b)
    var typed = <Box size: 5>
    inspect_typed(typed, typed)
    var numbers: int[] = [5]
    inspect_array(numbers, numbers)
    var sequence: int[] = [1, 2, 3]
    inspect_array_loop(sequence, sequence)
    let source = {arr: [1, 2]}
    var child = source.arr
    child[0] = 10
    print(source.arr)
    print("\n")
    var xs = [1, 2, 3]
    var a = xs or null
    var c = if (true) xs else null
    var d = match (1) { case 1: xs default: null }
    var e = xs that true
    a[0] = 99
    c[1] = 88
    d[2] = 77
    e[0] = 66
    print([xs, a, c, d, e])
    print("\n")
    var root = [1, 2]
    var via_or = root or null
    var via_if = if (true) root else null
    var via_match = match (1) { case 1: root default: null }
    var via_that = root that true
    root[0] = 9
    print([root, via_or, via_if, via_match, via_that])
    print("\n")
    var m = {}
    m["a"] = 1
    print(m.a)
    print("\n")
}
