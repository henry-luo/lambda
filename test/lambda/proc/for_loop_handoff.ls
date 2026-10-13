// D8.1.1v18: a `for` statement that is a direct statement of a `pn` body hands
// the activation to compiled code at a head test, resuming its walk on the
// T0 snapshot at the next position. Each shape below runs once per call; the
// tier-parity gtest lowers LAMBDA_LOOP_JIT_THRESHOLD so every loop hands off
// mid-walk, and the output must equal the pure interpreter's.

pn positions(xs) {
    var acc = []
    for i of xs {
        acc = acc ++ [i * 10]
    }
    acc
}

pn pairs(xs) {
    var acc = []
    for i, x of xs {
        acc = acc ++ [i + x]
    }
    acc
}

pn evens_total(xs) {
    var total = 0
    for x in xs where x % 2 == 0 {
        total = total + x
    }
    total
}

pn until_big(xs) {
    var acc = []
    for x in xs {
        if (x > 15) { break }
        acc = acc ++ [x]
    }
    [len(acc), acc[len(acc) - 1]]
}

pn skip_odds(xs) {
    var acc = 0
    for x in xs {
        if (x % 2 == 1) { continue }
        acc = acc + 1
    }
    acc
}

pn find_first(xs, target) {
    for i of xs {
        if (xs[i] == target) { return i * 100 }
    }
    -1
}

pn map_keys(m) {
    var names = []
    for k at m {
        names = names ++ [k]
    }
    names
}

pn child_positions(e) {
    var acc = []
    for i, c of e {
        acc = acc ++ [[i, c]]
    }
    acc
}

pn codes(text) {
    var out = []
    for i of text {
        out.push(ord(text[i]))
    }
    out
}

pn range_sum(n) {
    var s = 0
    for i in 0 to n {
        s = s + i
    }
    s
}

pn double_in_place(var arr) {
    for i of arr {
        arr[i] = arr[i] * 2
    }
    arr
}

pn rebind_source(n) {
    var xs = [for (k in 1 to n) k]
    var seen = 0
    for x in xs {
        xs = xs ++ [x]
        seen = seen + 1
    }
    [seen, len(xs)]
}

pn main() {
    let xs = [for (k in 1 to 20) k]
    print(positions(xs)); print("\n")
    print(pairs(xs)); print("\n")
    print(evens_total(xs)); print("\n")
    print(until_big(xs)); print("\n")
    print(skip_odds(xs)); print("\n")
    print(find_first(xs, 13)); print("\n")
    print(find_first(xs, 99)); print("\n")
    print(map_keys({a: 1, b: 2, c: 3, d: 4, e: 5, f: 6})); print("\n")
    print(child_positions(<p id: "x", "a" <b> "c" <i> "e" <u> "g">)); print("\n")
    print(codes("hello, world")); print("\n")
    print(range_sum(30)); print("\n")
    var arr = [for (k in 1 to 12) k]
    print(double_in_place(arr)); print("\n")
    print(arr); print("\n")
    print(rebind_source(9)); print("\n")
}
