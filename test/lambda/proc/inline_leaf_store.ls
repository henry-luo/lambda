// T30-5: a leaf with `var T[]` parameters writes through its caller's binding
// (S9.2.2). Inlining must keep that write-through, the copy-on-write of a
// shared array, and the error an out-of-range store produces.
pn set_pair(var a: int[], var b: int[], i: int, v: int) any {
    a[i] = v
    b[i] = v + 1
}

pn bump(var a: int[], i: int) any {
    a[i] = a[i] + 1
}

pn fill_row(var flags: bool[], i: int, on: bool) any {
    flags[i] = on
    flags[i + 1] = on
}

pn shift_all(var a: int[], var b: int[], n: int) any {
    var i: int = 0
    while (i < n) {
        set_pair(a, b, i, i * 10)
        i = i + 1
    }
}

pn main() {
    var x: int[] = [0, 0, 0, 0]
    var y: int[] = [9, 9, 9, 9]
    set_pair(x, y, 1, 5)
    print("pair=" ++ x ++ " " ++ y ++ "\n")
    bump(x, 1)
    print("bump=" ++ x ++ "\n")
    var flags: bool[] = fill(4, false)
    fill_row(flags, 1, true)
    print("flags=" ++ flags ++ "\n")
    // a shared array must be detached before the write reaches it
    var shared: int[] = [1, 2, 3]
    let alias = shared
    set_pair(shared, y, 0, 77)
    print("cow=" ++ shared ++ " " ++ alias ++ "\n")
    // a caller whose arrays are themselves `var` parameters writes through
    shift_all(y, x, 3)
    print("nested=" ++ y ++ " " ++ x ++ "\n")
    // an out-of-range store
    var small: int[] = [1, 2]
    set_pair(small, y, 5, 3)
    print("oob=" ++ small ++ "\n")
    print("done\n")
}
