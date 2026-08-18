// T19-4: two inference results that now select a NATIVE carrier without any
// annotation — an untyped array parameter specialized from the caller's local
// binding, and an unannotated local whose initializer is a proven int tree.
// These cases pin the results that must stay identical to the boxed lowering
// [SI3v2: inference never changes a type-error-free script's result].

// untyped params; `flags` is specialized through the caller's local binding
pn count_set(flags, n) {
    var hits = 0
    for i in 0 to n - 1 {
        if (flags[i]) { hits = hits + 1 }
    }
    return hits
}

// unannotated local on the int lane, mutated in a nested loop
pn tri(n) {
    var total = 0
    for i in 1 to n {
        var step = i + i
        var k = step - i
        while (k <= i) {
            total = total + k
            k = k + 1
        }
    }
    return total
}

pn main() {
    var flags = fill(8, true)
    flags[3] = false
    flags[5] = false

    // an inferred int-lane local must still SATURATE, not wrap
    var big = 9007199254740991 + 9007199254740991

    // ...and must still widen when a later value does not fit its lane
    var w = 2 + 3
    w = 1.5
    var z = 4 + 4
    z = "text"

    // a second callee shape: the same helper over a float-filled array
    var fl = fill(4, 1.0)

    print([
        count_set(flags, 8),   // 6
        tri(5),                // 15
        big,                   // inf (int53 saturation)
        w,                     // 1.5
        z,                     // "text"
        count_set(fl, 4)       // 4 — every 1.0 is truthy
    ])
}
