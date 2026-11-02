// D4.4.4v4, S9.1.2: a closed kernel can reuse storage witnesses only while owners stay private.
pn add_four(var a: float[], step: float) {
    var i: int = 0
    while (i < 4) { a[i] = a[i] + step; i = i + 1 }
}
pn repeat_add(var a: float[], cycles: int) {
    var i: int = 0
    while (i < cycles) { add_four(a, 0.5); i = i + 1 }
}
pn positive_sum(a: float[]) bool^ {
    var total: float = 0.0
    var i: int = 0
    while (i < 4) { total = total + a[i]; i = i + 1 }
    return total > 0.0
}
pn count_positive(a: float[]) int {
    var count: int = 0
    var i: int = 0
    while (i < 3) {
        if (positive_sum(a)) { count = count + 1 }
        i = i + 1
    }
    return count
}
pn replacing(var a: float[]) {
    var i: int = 0
    while (i < 2) {
        add_four(a, 0.5)^
        a = fill(4, 2.0)^
        i = i + 1
    }
}
pn main() {
    var a: float[] = fill(4, 1.0)^
    repeat_add(a, 3)^
    print(a); print(" "); print(count_positive(a)); print("\n")
    let snapshot: float[] = a
    repeat_add(a, 2)^
    print(a); print(" "); print(snapshot); print("\n")
    var short: float[] = [1.0]
    repeat_add(short, 0)
    print(short); print(" ")
    repeat_add(short, 2)
    print(short); print("\n")
    replacing(a)^
    print(a); print(" "); print(snapshot); print("\n")
}
