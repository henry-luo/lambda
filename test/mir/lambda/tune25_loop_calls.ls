// D5.3.4/D8.2.6: only total scalar calls with loop-invariant operands move.
pn local_while(n: int, value: float) float {
    let scale = value * 2.0
    var total = 0.0
    var i = 0
    while (i < n) {
        total = total + abs(scale)
        i = i + 1
    }
    return total
}
pn counted(n: int, value: float) float {
    let scale = value * 2.0
    var total = 0.0
    for (i in 1 to n) { total = total + abs(scale) }
    return total
}
pn changing(n: int, value: float) float {
    var scale = value
    var total = 0.0
    for (i in 1 to n) {
        total = total + abs(scale)
        scale = scale + 1.0
    }
    return total
}
fn retained(n: int) => [for (i in 1 to n) abs(float(i))]
pn touch(var count: int[]) { count[0] = count[0] + 1 }
pn effects() {
    var count: int[] = [0]
    for (i in 1 to 3) { touch(count) }
    return count[0]
}
pn main() {
    print([local_while(0, -2.5), local_while(4, -2.5),
        counted(4, -2.5), changing(4, -2.5), retained(3), effects()])
}
