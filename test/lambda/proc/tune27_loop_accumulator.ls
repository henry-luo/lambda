// Tune27 T27-9: a loop accumulator's sentinel operand guard is dropped inside a
// guarded loop copy, and a slow arm transfers to the generic sibling at the
// next loop head. Accumulators that saturate mid-loop, sentinel elements, an
// `if`-guarded update, `continue`, a right-hand accumulator and subtraction
// must all give the interpreter's answers (S4.1.2). The helpers run hot so the
// auto tier promotes them.
pn tune27_big(k: int) int { return 9007199254740990 + k }

pn tune27_sum(a: int[], n: int) int {
    var s = 0
    var i = 0
    while (i < n) {
        s = s + a[i]
        i = i + 1
    }
    return s
}

pn tune27_sum_skip(a: int[], n: int) int {
    var s = 0
    var c = 0
    var i = 0
    while (i < n) {
        i = i + 1
        if (a[i - 1] < 0) { continue }
        s = a[i - 1] + s
        c = c - a[i - 1]
    }
    return s * 1000 + c
}

pn tune27_countdown(a: int[], n: int) int {
    var s = 100
    var i = 0
    while (i < n) {
        if (i % 2 == 0) { s = s - a[i] }
        i = i + 1
    }
    return s
}

pn main() {
    let pinf = tune27_big(5)
    let ninf = 0 - pinf
    var plain: int[] = [1, 2, 3, 4]
    var sat: int[] = [9007199254740990, 5, 3, -2]
    var pair: int[] = [pinf, ninf, 7, 1]
    var neg: int[] = [ninf, ninf, 1]
    var mixed: int[] = [1, -1, pinf, -3, ninf, 2]
    var r = []
    var k = 0
    while (k < 30) {
        r = [tune27_sum(plain, 4), tune27_sum(sat, 4), tune27_sum(pair, 4),
             tune27_sum(neg, 3), tune27_sum_skip(mixed, 6),
             tune27_sum_skip(plain, 4), tune27_countdown(pair, 4),
             tune27_countdown(plain, 4)]
        k = k + 1
    }
    print(r)
    print("\n")
}
