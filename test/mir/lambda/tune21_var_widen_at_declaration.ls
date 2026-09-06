// An unannotated `var` bound on a native scalar lane whose body later assigns
// it a value the assignment cascade widens (an int local receiving a float)
// is boxed at its DECLARATION, the one point that dominates every read and
// write. The cascade used to switch the binding to a fresh boxed register at
// the assignment itself: inside an `if` arm or a loop body that register is
// undefined on every path that skips the assignment (f(false), and the first
// outer iteration of fft's bit reversal, where `j >= m` is false so the
// inner loop never runs before `j = j + m`), while reads emitted before the
// assignment kept the stale native register. The GC root then held garbage
// and fn_add received a non-Item. Both bodies below therefore lower `j` as a
// boxed Item throughout: the float subtraction is fn_sub, never a native
// dsub on an int-lane register, and the comparison is fn_ge.
pn f(flag) {
    var j = 0
    if (flag) { j = j - 0.5 }
    return j + 1
}
pn g(n) {
    var j = 0
    var m = n / 2
    while (m >= 2 and j >= m) { j = j - m; m = m / 2 }
    j = j + m
    return j
}
// the int lane's null sentinel is out of band and reaches the slow arm, which
// must keep absence absent (S7) instead of saturating it to inf
pn h() {
    var a = fill(3, 5)
    var i = 1
    return a[i + 9] + 1
}
pn main() {
    print(f(false)); print(" "); print(f(true)); print(" ")
    print(g(8)); print(" "); print(g(2)); print(" "); print(g(64)); print(" ")
    print(h()); print("\n")
}
