// Tune27 T27-9 / S4.1.2: `int` sentinel operands must not be laundered back
// into the band by the native fast arm. Two sentinels can wrap to a small
// word (+inf + -inf = 0 in i64) and a sentinel times zero is 0; both are
// indeterminate forms that give nan. The helpers run hot so the auto tier
// promotes them onto the MIR lane the forms used to break.
pn tune27_add(a: int, b: int) int { return a + b }
pn tune27_sub(a: int, b: int) int { return a - b }
pn tune27_mul(a: int, b: int) int { return a * b }

pn main() {
    let hi = 9007199254740991
    let lo = -9007199254740991
    var forms = []
    var i: int = 0
    while (i < 40) {
        let pinf = tune27_add(hi, 1)
        let ninf = tune27_sub(lo, 1)
        let poison = tune27_mul(pinf, 0)
        forms = [tune27_add(pinf, ninf), tune27_sub(pinf, pinf), tune27_mul(pinf, 0),
            tune27_mul(0, ninf), tune27_add(poison, pinf), tune27_add(ninf, ninf),
            tune27_mul(pinf, -1), tune27_add(pinf, 5), tune27_mul(i, 2)]
        i = i + 1
    }
    print(forms)
    print("\n")
}
