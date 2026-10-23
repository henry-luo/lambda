// D3.3.3/D8.3.3: a complete loop witness preserves numeric lanes inductively.
pn least(a, b, c) {
    if (a < b) { if (a < c) { return a }; return c }
    if (b < c) { return b }
    return c
}
pn rows(n) {
    var previous = fill(n + 1, 0)
    var current = fill(n + 1, 0)
    var j = 1
    while (j <= n) {
        current[j] = least(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + 1)
        j = j + 1
    }
    return current
}
pn changing_lane() {
    var values = fill(3, 0)
    var i = 0
    while (i < 3) { values[i] = null; i = i + 1 }
    return values
}
pn uncertain_argument() {
    var values = fill(3, 0)
    var i = 1
    while (i < 3) { values[i] = least(values[i - 1], 1, null); i = i + 1 }
    return values
}
pn retained_owner() {
    var values = fill(3, 0)
    let old = values
    var i = 0
    while (i < 3) { values[i] = i + 1; i = i + 1 }
    return [old, values]
}
pn short_swap() {
    var values = [1, 2, 3]
    var lo = 0
    var hi = 2
    while (lo < hi) {
        let saved = values[lo]
        values[lo] = values[hi]
        values[hi] = saved
        lo = lo + 1
        hi = hi - 1
    }
    return values
}
pn main() {
    print([rows(0), rows(1), rows(4)]); print("\n")
    print(changing_lane()); print("\n")
    print(uncertain_argument()); print("\n")
    print(retained_owner()); print("\n")
    print(short_swap()); print("\n")
}
