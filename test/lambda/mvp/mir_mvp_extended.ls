// S12.2.1, S7.1.1v3, S9.1.2: inferred locals preserve absence and changing values; copies remain snapshots.
pn native_sine(x: float) float { return math.sin(x) }
pn dynamic_sine(x: any) any { return math.sin(x) }
pn bit_or(a: int, b: int) any { return bor(a, b) }
pn generic_or(a: any, b: any) any { return bor(a, b) }
pn optional_read(a: float[]) any {
    var sample = a[0]
    print(sample); print(" ")
    sample = a[3]
    print(sample); print("\n")
}
pn make_numbers(n: int) int[] {
    var a: int[] = fill(n, 0)
    a[0] = n
    return a
}
pn main() {
    var x = 0.0
    var i = 0
    var flag = false
    while (i < 3) { x = x + 0.25; i = i + 1; flag = true }
    print(x); print(" "); print(i); print(" "); print(flag); print("\n")
    var floats: float[] = [2.5, 4.5]
    optional_read(floats)
    print(native_sine(0.0)); print(" "); print(dynamic_sine(0.0)); print(" ")
    print(dynamic_sine(null)); print(" "); print(dynamic_sine("bad")); print("\n")
    let nonfinite = native_sine(1.0 / 0.0)
    print(nonfinite != nonfinite); print(" "); print(1.0 / native_sine(-0.0) < 0.0); print("\n")
    print(bit_or(8, 3)); print(" "); print(bit_or(-8, 3)); print(" ")
    print(generic_or(8, 3)); print(" "); print(generic_or(null, 3)); print(" ")
    print(generic_or("bad", 3)); print(" "); print(bor(shl(1, -1), 3)); print("\n")
    print(ord(chr(955))); print(" "); print(ord(chr(128578))); print(" ")
    print(ord("")); print(" "); print(ord(null)); print(" "); print(len(chr(-1))); print("\n")
    print(chr(shl(1, -1))); print(" "); print(ord(shl(1, -1))); print("\n")
    var a = fill(3, 0)
    let original = a
    a[0] = 1.5
    a[1] = "changed"
    let mixed = a
    a[1] = "again"
    print(original[0]); print(" "); print(a[0]); print(" "); print(mixed[1]); print(" "); print(a[1]); print("\n")
    var returned: int[] = make_numbers(3)
    let saved: int[] = returned
    var other: int[] = make_numbers(5)
    returned[0] = other[0]
    print(saved[0]); print(" "); print(returned[0]); print("\n")
}
