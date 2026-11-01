// S4.3, S7.1.1v3, S9.1.2–S9.1.3: numeric lanes preserve absence and snapshots.
let UNIT = 1.5
let SCALE = 2.0 * UNIT
pn absent(a: float[], i: int) any { return (a[i] + 1.0) * 2.0 }
pn negate(a: float[], i: int) any { return -a[i] }
pn dynamic(a: float[], i: any) any { return a[i] }
pn write_absent(var a: float[]) { a[0] = a[9] + 1.0; print("UNEXPECTED WRITE\n") }
pn boundary(a: float[]) { var scalar: float = a[9]; print("UNEXPECTED BOUNDARY\n") }
pn borrow(var a: float[], snapshot: float[]) float {
    a[0] = SCALE
    return snapshot[0]
}
pn add_three(var a: float[], b: float[]) {
    var i: int = 0
    while (i < 3) { a[i] = a[i] + b[i]; i = i + 1 }
}
pn halve(x: float) float { return x / 2 }
pn generic_halve(x: any) any { return x / 2 }
pn nested_counter(var a: float[]) {
    var i: int = 0
    while (i < 3) {
        var j: int = 0
        while (j < 2) {
            a[i] = 7.0
            i = 4
            j = j + 1
        }
    }
    print("UNEXPECTED NESTED TAIL\n")
}
pn main() {
    var a: float[] = [-0.0, 1e-310, SCALE, math.sqrt(-1.0)]
    print(1.0 / a[0] < 0.0); print(" "); print(a[1] == 1e-310); print(" ")
    print(a[2] == 3.0); print(" "); print(a[3] != a[3]); print(" "); print(a[3] == null); print("\n")
    print(absent(a, 2)); print(" "); print(absent(a, -1)); print(" "); print(absent(a, 4)); print("\n")
    print(negate(a, 4)); print(" "); print(negate(a, 2)); print(" "); print(dynamic(a, 1) == 1e-310); print("\n")
    print(dynamic(a, "bad")); print(" "); print(dynamic(a, 4294967296)); print("\n")
    let old: float[] = a
    print(borrow(a, a)); print(" "); print(a[0]); print(" "); print(1.0 / old[0] < 0.0); print("\n")
    write_absent(a)
    boundary(a)
    print(a[0]); print("\n")
    var empty: float[] = fill(0, 0.0)
    print(absent(empty, 0)); print("\n")
    var ints: int[] = [1, 2, 3]
    var flags: bool[] = [true, false]
    print(ints[2]); print(" "); print(flags[1]); print("\n")
    var dest: float[] = fill(3, 1.0)
    var src: float[] = fill(3, 2.0)
    add_three(dest, src)
    let saved: float[] = dest
    add_three(dest, src)
    print(saved[2]); print(" "); print(dest[2]); print("\n")
    var short: float[] = fill(1, 4.0)
    add_three(dest, short)
    print(dest[0]); print(" "); print(dest[1]); print("\n")
    print(halve(1e-310) == generic_halve(1e-310)); print(" ")
    print(halve(5e-324) == generic_halve(5e-324)); print(" ")
    print(1.0 / halve(-0.0) < 0.0); print("\n")
    nested_counter(dest)
    print(dest[0]); print(" "); print(dest[2]); print("\n")
}
