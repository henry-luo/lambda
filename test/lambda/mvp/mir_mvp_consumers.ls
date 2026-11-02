// S7.1.1v3, S9.1.2, S12.2.1: fused consumers preserve absence, snapshots and inferred unions.
pn identity(s: string) string { return s }
pn quoted(s: string) string { return "<" ++ identity(s) ++ ">" }
pn twice(s: string, unused: string) string { return s ++ s }
pn marked(s: string) string { print(s); return s }
pn add(a: int, b: int) int { return a + b }
pn checked_inline(a: int[], key: int) int {
    a[0] = add(a[0], 2)
    a[0] = add(a[key], 2)
    return a[0]
}
pn square_end(start: int, limit: int) int {
    var i: int = start
    while (i * i <= limit) { i = i + 1 }
    return i
}
pn product_end(factor: int, limit: int) int {
    var i: int = 0
    while (factor * i < limit) { i = i + 1 }
    return i
}
pn small_get(a: int[], i: int) int { return a[i] }
pn inline_checks() {
    var a: int[] = [7]
    print(small_get(a, 0)); print(" ")
    if (true) { let bad: int = small_get(a, 5); print("UNEXPECTED INLINE\n") }
    print(a[0]); print("\n")
}
pn truth_count(a: bool[], start: int, end: int, initial: int) int {
    var count: int = initial
    var i: int = start
    while (i <= end) { if (a[i]) { count = count + 1 }; i = i + 1 }
    return count * 100 + i
}
pn character(s: string, i: int) any { return ord(s[i]) }
pn same(a: string, i: int, b: string, j: int) bool { return a[i] == b[j] }
pn different(a: string, i: int, b: string, j: int) bool { return a[i] != b[j] }
pn rotate(n: int, a: int, b: int, c: int) int {
    if (n == 0) { return a * 100 + b * 10 + c }
    return rotate(n - 1, b, c, a)
}
pn descending(n: float, initial: float) float {
    var s: float = initial
    while (n >= 0.0) { s = s + n; n = n - 1.0 }
    return s
}
pn inferred(a: float[]) {
    var x = a[0]
    x = x + 0.25
    print(x); print(" ")
    x = a[5]
    print(x + 1.0); print(" ")
    var y = a[0]
    y = "changed"
    print(y); print("\n")
}
pn scalar_math(a: int[]) {
    print(abs(a[0])); print(" "); print(abs(a[5])); print(" ")
    print(floor(a[0])); print(" "); print(abs(int(1.0 / 0.0))); print(" ")
    let poison: int = int(0.0 / 0.0)
    print(abs(poison) != abs(poison)); print("\n")
}
pn dynamic_math(a: any, b: any) {
    print(a + b); print(" "); print(a - b); print(" "); print(a * b); print(" ")
    print(a % b); print(" "); print(a == b); print(" "); print(a < b); print("\n")
}
pn dynamic_arrays() {
    var a = fill(3, 0)
    a[0] = a[0] + 7
    let saved = a
    a[0] = (a[0] + 4) % 10
    print(saved[0]); print(" "); print(a[0]); print(" ")
    a[1] = 1.5
    print(a[1] + 2); print(" ")
    a = "abc"
    print(a[1]); print(" ")
    a = fill(3, 2)
    a[2] = a[0] * 3
    print(a[2]); print(" "); print(a[-1]); print("\n")
}
pn rotate_arrays(observe: bool) {
    var a: int[] = [1]
    var b: int[] = [2]
    var c: int[] = [3]
    let saved: int[] = a
    var i: int = 0
    while (i < 4) {
        var scratch = a
        a = b
        b = c
        c = scratch
        i = i + 1
    }
    a[0] = 8
    print(a[0]); print(" "); print(b[0]); print(" "); print(c[0]); print(" "); print(saved[0]); print(" ")
    var observed = a
    a = b
    b = observed
    b[0] = 9
    if (observe) { print(observed[0]); print(" ") }
    print(b[0]); print("\n")
}
pn main() {
    var s: string = ""
    var i: int = 0
    while (i < 50) { s = s ++ "x"; i = i + 1 }
    let saved: string = s
    s = s ++ "y"
    let passed: string = identity(s)
    s = s ++ "z" ++ "!"
    print(len(saved)); print(" "); print(len(passed)); print(" "); print(len(s)); print("\n")
    s = s ++ s
    print(len(saved)); print(" "); print(len(passed)); print(" "); print(len(s)); print("\n")
    print(character("abc", 1)); print(" "); print(character("abc", -1)); print(" ")
    print(character("abc", 3)); print(" "); print(character("λ🙂", 1)); print("\n")
    print(same("abc", 1, "b", 0)); print(" "); print(different("abc", 1, "c", 0)); print(" ")
    print(same("λ🙂", 1, "🙂", 0)); print(" "); print(same("a", 8, "b", -1)); print("\n")
    let nul: string = chr(0)
    print(character(nul, 0)); print(" "); print(same(nul, 0, "", 0)); print(" ")
    print(same(nul, 0, nul, 0)); print("\n")
    var a: float[] = [2.0]
    inferred(a)
    print(rotate(10000, 1, 2, 3)); print("\n")
    print(descending(5.0, 0.0)); print(" "); print(descending(2.5, 0.0)); print(" ")
    print(descending(5.0, 3.0)); print(" "); print(descending(-2.0, 3.0)); print(" ")
    print(1.0 / descending(-0.0, -0.0) < 0.0); print("\n")
    var ints: int[] = [-7]
    scalar_math(ints)
    dynamic_math(7, 3)
    dynamic_math(-7, 3)
    dynamic_math(1.5, 2.0)
    dynamic_math(null, 2)
    dynamic_arrays()
    rotate_arrays(true)
    let fused: string = quoted(marked("a")) ++ ":" ++ twice(marked("b"), marked("c"))
    print(" "); print(fused); print("\n")
    print(checked_inline(ints, 0)); print(" ")
    if (true) { let failed: int = checked_inline(ints, 9); print("UNEXPECTED STORE\n") }
    print(ints[0]); print("\n")
    print(square_end(0, 15)); print(" "); print(square_end(0, 16)); print(" ")
    print(square_end(-5, 16)); print(" "); print(square_end(-4, 16)); print(" ")
    print(square_end(0, -1)); print(" "); print(product_end(3, 16)); print(" ")
    print(product_end(2, 16)); print("\n")
    inline_checks()
    var flags: bool[] = [true, false, true, false, true]
    print(truth_count(flags, 0, 4, 0)); print(" "); print(truth_count(flags, 1, 3, 0)); print(" ")
    print(truth_count(flags, 0, 7, 0)); print(" "); print(truth_count(flags, -1, 1, 0)); print(" ")
    print(truth_count(flags, 0, 4, 5)); print(" "); print(truth_count(flags, 4, 2, 0)); print("\n")
}
