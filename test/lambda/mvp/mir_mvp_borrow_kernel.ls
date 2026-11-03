// a readonly sibling may borrow only while the writer is exclusive (S9.1.3v2).
pn step(var a: float[], snapshot: float[]) float {
    a[0] = a[0] + 1.0
    return snapshot[0]
}

pn repeat(var a: float[], b: float[], n: int) float {
    var total: float = 0.0
    var i: int = 0
    while (i < n) { total = total + step(a, b); i = i + 1 }
    return total
}

pn main() {
    var a: float[] = [1.0]
    var b: float[] = [10.0]
    print(repeat(a, b, 3)); print(" "); print(a[0]); print(" "); print(b[0]); print("\n")
    print(step(a, a)); print(" "); print(a[0]); print("\n")
    let alias: float[] = a
    print(repeat(a, alias, 3)); print(" "); print(a[0]); print(" "); print(alias[0]); print("\n")
    var empty: float[] = []
    let rejected = step(a, empty)
    print(match rejected { case error: true default: false }); print(" ")
    print(a[0]); print("\n")
}
