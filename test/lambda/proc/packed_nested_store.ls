// T29-5: a comprehension of equal numeric rows is one packed N-D array. A
// nested index store writes its leaf at the full coordinate instead of
// building a row view per store; the value semantics are unchanged
// (S9.1.2, S9.2.2, D3.3.3v3).
pn fill_grid(var m: (int*)*, n: int) any {
    var y: int = n - 1
    while (y >= 0) {
        var x: int = n - 1
        while (x >= 0) {
            m[x][y] = x * 10 + y
            x = x - 1
        }
        y = y - 1
    }
}

pn local_grid(n: int) int {
    var m = [for (i in 0 to n - 1) fill(n, 0)]
    m[1][2] = 7
    m[2][0] = m[1][2] + 1
    return m[1][2] * 100 + m[2][0]
}

pn snapshot(n: int) int {
    var m = [for (i in 0 to n - 1) fill(n, 0)]
    let snap = m
    m[0][0] = 5
    return m[0][0] * 10 + snap[0][0]
}

pn cube(n: int) int {
    var c = [for (i in 0 to n - 1) [for (j in 0 to n - 1) fill(n, 0)]]
    c[1][0][1] = 3
    c[0][1][1] = c[1][0][1] * 2
    return c[1][0][1] * 10 + c[0][1][1]
}

pn mixed_value(n: int) int {
    var m = [for (i in 0 to n - 1) fill(n, 0)]
    m[0][0] = "s"
    return len(m)
}

// plain-parameter snapshot: the first store detaches through the compact
// COW store; the caller's array is untouched (S9.1.3)
pn bump_copy(a: int[], i: int, v: int) int {
    a[i] = v
    return a[i]
}

pn flags(n: int) int {
    var f: bool[] = fill(n, true)
    f[1] = false
    return if (f[0] and not f[1]) 1 else 0
}

pn main() {
    var g: (int*)* = [for (i in 0 to 2) fill(3, 0)]
    fill_grid(g, 3)
    print("grid=" ++ g[2][1] ++ " " ++ g[0][2] ++ "\n")
    print("local=" ++ local_grid(3) ++ " snapshot=" ++ snapshot(3) ++
        " cube=" ++ cube(2) ++ "\n")
    let mixed = mixed_value(2)
    print("mixed=" ++ (if (mixed is error) "error" else string(mixed)) ++ "\n")
    var base: int[] = [1, 2, 3]
    print("copy=" ++ bump_copy(base, 1, 9) ++ " base=" ++ base[1] ++ "\n")
    print("flags=" ++ flags(3) ++ "\n")
}
