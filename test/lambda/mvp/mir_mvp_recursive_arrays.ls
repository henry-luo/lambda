// S9.1.2–S9.1.3: recursive readers may borrow; retained or mutated values remain snapshots.
pn sum_tail(a: int[], n: int) int {
    if (n == 0) { return 0 }
    return a[n - 1] + sum_tail(a, n - 1)
}
pn even_read(a: int[], n: int) int {
    if (n == 0) { return a[0] }
    return odd_read(a, n - 1)
}
pn odd_read(a: int[], n: int) int {
    if (n == 0) { return a[1] }
    return even_read(a, n - 1)
}
pn retain(a: int[], n: int) int[] {
    if (n == 0) { return a }
    return retain(a, n - 1)
}
pn plain_write(a: int[], n: int) int {
    a[0] = a[0] + 1
    if (n == 0) { return a[0] }
    return plain_write(a, n - 1)
}
pn observe_and_change(a: int[], var b: int[]) int {
    var before: int = a[0]
    b[0] = b[0] + 10
    return a[0] * 10 + before
}
// S7.1.1v3: absence affects truth without creating a function error result.
pn available(a: bool[], i: int) int {
    if (a[i]) { return 1 }
    return 0
}
// S4.1.1–S4.1.2: induction must close over every incoming edge, including poison.
pn bounded_up(n: int) int {
    if (n == 12) { return n }
    return bounded_up(n + 1)
}
pn bounded_down(n: int) int {
    if (n <= 0) { return n }
    return bounded_down(n - 1)
}
pn grows(n: int, left: int) int {
    if (left == 0) { return n }
    return grows(n * 2, left - 1)
}
pn near_limit(n: int) int {
    if (n == 9007199254740991) { return n }
    return near_limit(n + 1)
}
pn search(a: int[], n: int) int {
    if (n < 0) { return -1 }
    var current: int = a[n]
    if (current == 5) { return n }
    return search(a, n - 1)
}
pn touch(var a: int[], value: int) any { a[0] = value }
pn count_up(var a: int[], n: int) int {
    touch(a, n)
    if (n == 4) { return a[0] }
    return count_up(a, n + 1)
}
pn main() {
    var a: int[] = [2, 3, 5]
    print(sum_tail(a, 3)); print("\n")
    print(even_read(a, 3)); print("\n")
    print(plain_write(a, 2)); print("\n")
    print(a[0]); print("\n")
    print(observe_and_change(a, a)); print("\n")
    var kept: int[] = retain(a, 2)
    a[0] = 99
    print(kept[0]); print("\n")
    print(a[0]); print("\n")
    var flags: bool[] = [true, false]
    print(available(flags, 0)); print("\n")
    print(available(flags, 1)); print("\n")
    print(available(flags, -1)); print("\n")
    print(available(flags, 2)); print("\n")
    print(available(flags, int(inf))); print("\n")
    print(bounded_up(0)); print("\n")
    print(bounded_up(11)); print("\n")
    print(bounded_down(7)); print("\n")
    print(bounded_down(-3)); print("\n")
    print(grows(4503599627370496, 2)); print("\n")
    print(near_limit(9007199254740990)); print("\n")
    var searched: int[] = [5, 2, 3, 4]
    print(search(searched, 3)); print("\n")
    if (true) {
        let missing: int = search(searched, 8)
        print("UNEXPECTED SEARCH\n")
    }
    print("after search\n")
    var counter: int[] = [9]
    let old: int[] = counter
    print(count_up(counter, 0)); print("\n")
    print(counter[0]); print("\n")
    print(old[0]); print("\n")
    if (true) {
        var short: int[] = []
        let failed: int = count_up(short, 0)
        print("UNEXPECTED SHORT\n")
    }
    print("after short\n")
}
