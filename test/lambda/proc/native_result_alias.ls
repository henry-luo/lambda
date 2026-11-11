// LR12-47 (S9.1.2, S9.3.1): a system function or `++` that places its argument's
// items into its result captures them. A write through the argument afterwards
// leaves the result unchanged, and a write through the result leaves the
// argument unchanged. Every number below stays 1.
pn main() {
    // writes through the argument after the call
    var a = [{n: 1}, {n: 2}, {n: 3}]
    let reversed = reverse(a)
    let taken = take(a, 2)
    let dropped = drop(a, 0)
    let sliced = slice(a, 0, 2)
    let ranged = a[0 to 1]
    let sorted = sort(a, (x) => x.n)
    let uniq = unique(a)
    let kept = except(a, [{n: 3}])
    let zipped = zip(a, [0, 1, 2])
    let joined = a ++ [{n: 4}]
    a[0].n = 9
    let through_argument = [reversed[2].n, taken[0].n, dropped[0].n, sliced[0].n,
        ranged[0].n, sorted[0].n, uniq[0].n, kept[0].n, zipped[0][0].n, joined[0].n]

    var item = {n: 1}
    let filled = fill(2, item)
    item.n = 9

    // writes through the result
    let b = [{n: 1}, {n: 1}]
    var r1 = reverse(b)
    r1[1].n = 9
    var r2 = sort(b, (x) => x.n)
    r2[0].n = 9
    var r3 = unique(b)
    r3[0].n = 9
    var r4 = zip(b, [0, 1])
    r4[0][0].n = 9
    var r5 = reduce(b, (acc, x) => x)
    r5.n = 9
    var r6 = b ++ ({n: 5}, {n: 6})
    r6[0].n = 9
    var r7 = null ++ b
    r7[0].n = 9
    let list_item = ({n: 1}, {n: 1})
    var r8 = fill(2, list_item)
    r8[0].n = 9

    // a consuming join appends the right operand's items
    var acc = [{n: 1}]
    let extra = [{n: 1}]
    acc = acc ++ extra
    acc[1].n = 9

    return {
        through_argument: through_argument,
        filled: filled[1].n,
        through_result: [b[0].n, b[1].n, list_item[0].n],
        extra: extra[0].n,
        written: [r1[1].n, r2[0].n, r3[0].n, r4[0][0].n, r5.n, r6[0].n, r7[0].n, r8[0].n, acc[1].n]
    }
}
