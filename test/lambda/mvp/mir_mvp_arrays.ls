// S7.1.1v3, S9.1.2–S9.1.3, D4.4.4v4: indexed absence, snapshots and borrowed replacement.
pn replace(var a: int[]) {
    a = fill(3, 9)^
    a[1] = 7
}
pn forward(var a: int[]) { replace(a) }
pn mixed(snapshot: int[], var a: int[]) int {
    a[0] = 41
    return snapshot[0]
}
pn mixed_reverse(var a: int[], snapshot: int[]) int {
    a[0] = 42
    return snapshot[0]
}
pn churn(var a: int[]) {
    var i: int = 0
    while (i < 100) {
        let garbage = fill(128, i)
        a[0] = a[0] + 1
        i = i + 1
    }
}
pn absent(a: int[], index: int) any {
    return a[index] + 1
}
pn float_store(var a: float[]) { a[0] = 1e-310 }
pn rejected_write(var a: int[], key: any, candidate: any) {
    a[key] = candidate
    print("UNEXPECTED WRITE TAIL\n")
}
pn reduce(n: int, first: int, initial: int, seed: int) any {
    var a: int[] = fill(9, seed)^
    var i: int = first
    var sum: int = initial
    while (i < n) {
        sum = sum + a[i]
        i = i + 1
    }
    return sum
}
pn nested_argument(var a: int[]) int^ { a = fill(2, 11)^; return a[0] }
pn store_from(var a: int[], n: int) { a[0] = n }
pn mutating_key(var a: int[]) int { a[0] = 99; return 0 }
pn changed_counter() {
    var a: int[] = fill(3, 0)
    var i: int = 0
    while (i < 3) {
        i = i + 4
        a[i] = 1
    }
    print("UNEXPECTED CHANGED COUNTER\n")
}
pn detached_store_loop() {
    var a: int[] = fill(4, 0)^
    let snapshot: int[] = a
    var i: int = 0
    while (i < 4) {
        a[i] = i + 1
        i = i + 1
    }
    print(snapshot[3]); print(" "); print(a[3]); print("\n")
}
pn empty_fill(candidate: any) int^ { var a: int[] = fill(0, candidate); return len(a) }
pn rejected_fill(count: any, candidate: any) {
    var a: int[] = fill(count, candidate)
    print("UNEXPECTED FILL TAIL\n")
}
pn range_sum(first: int, finish: int) int {
    var sum: int = 0
    for i in first to finish {
        if (i == 2) { continue }
        if (i == 5) { break }
        sum = sum + i
    }
    return sum
}
pn main() {
    var a: int[] = fill(3, 1)
    let saved: int[] = a
    a[1] = 5
    print(saved[1]); print(" "); print(a[1]); print("\n")
    print(mixed(a, a)); print(" "); print(a[0]); print("\n")
    print(mixed_reverse(a, a)); print(" "); print(a[0]); print("\n")
    forward(a)^
    print(a[0]); print(" "); print(a[1]); print(" "); print(saved[0]); print("\n")
    churn(a)^
    print(a[0]); print(" "); print(a[1]); print("\n")
    print(absent(a, -1)); print(" "); print(absent(a, 3)); print(" "); print(absent(a, 4294967296)); print("\n")
    var flags: bool[] = fill(2, true)
    flags[1] = false
    print(flags[0]); print(" "); print(flags[1]); print(" "); print(flags[2]); print("\n")
    if (flags[2]) { print("UNEXPECTED\n") }
    print(not flags[2]); print(" "); print(flags[2] == null); print("\n")
    var empty: int[] = fill(0, 0)
    print(empty[0]); print("\n")
    var floats: float[] = fill(2, 0.0)
    let before: float[] = floats
    float_store(floats)^
    print(floats[0] == 1e-310); print(" "); print(before[0] == 0.0); print("\n")
    print(range_sum(1, 8)); print(" "); print(range_sum(8, 1)); print(" ")
    print(range_sum(9007199254740991, 9007199254740991)); print("\n")
    rejected_write(a, -1, 5)^
    rejected_write(a, 4294967296, 5)^
    rejected_write(a, "bad", 5)^
    rejected_write(a, 0, "bad")^
    print(a[0]); print(" "); print(saved[0]); print("\n")
    var n: int = 0
    while (n <= 9) {
        print(reduce(n, 0, 0, 3)); print(" ")
        n = n + 1
    }
    print(reduce(9, 1, 0, 3)); print(" "); print(reduce(9, 0, 7, 3)); print(" "); print(reduce(9, 0, 0, -3)); print("\n")
    if (reduce(10, 0, 0, 3)^) { print("UNEXPECTED EXTENT\n") }
    if (reduce(9, -1, 0, 3)^) { print("UNEXPECTED NEGATIVE INDEX\n") }
    changed_counter()
    print("checked reduction fallback\n")
    var other: int[] = fill(2, 2)
    store_from(a, nested_argument(other))^
    print(a[0]); print(" "); print(other[0]); print(" ")
    print(a[mutating_key(a)]); print(" "); print(a[0]); print("\n")
    detached_store_loop()
    print(empty_fill("unused")); print("\n")
    rejected_fill(-1, 1)
    rejected_fill(2, "bad")
    print("checked construction fallback\n")
}
