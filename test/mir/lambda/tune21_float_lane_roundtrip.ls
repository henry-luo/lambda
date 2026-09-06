// T21-2c: mixed int/float arithmetic over inferred parameters stays in the
// double lane (no fn_add/fn_float/fn_div), float() of that tree is the
// identity, an untyped pn whose returns are a reassigned float local or a
// float literal publishes the raw double return lane, and a comparison over
// such an inferred float local feeds `and` natively (no is_truthy).
pn eval_a(i, j) {
    return 1.0 / float((i + j) * (i + j + 1) / 2 + i + 1)
}
pn first_root(a, b, c) {
    var disc = b * b - 4.0 * a * c
    if (disc < 0.0) { return -1.0 }
    var t = (-b - math.sqrt(disc)) / (2.0 * a)
    if (t > 0.001) { return t }
    t = (-b + math.sqrt(disc)) / (2.0 * a)
    if (t > 0.001) { return t }
    return -1.0
}
pn count_hits(n) {
    var hits = 0
    var i = 0
    while (i < n) {
        var t = first_root(1.0, -3.0, float(i))
        if (t > 0.0 and t < 2.5) { hits = hits + 1 }
        i = i + 1
    }
    return hits
}
pn main() {
    print(eval_a(1, 2)); print(" "); print(eval_a(0, 0)); print(" ")
    print(first_root(1.0, -3.0, 2.0)); print(" "); print(first_root(1.0, 0.0, 1.0)); print(" ")
    print(count_hits(4)); print("\n")
}
