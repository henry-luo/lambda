// Tune13 P1: a discarded for-statement executes its body without materializing
// a comprehension output, while an observed for-expression keeps that output.

pn tune13_discard(n: int) int {
    var total: int = 0
    for i in 0 to n {
        total = total + i
    }
    return total
}

fn tune13_observed() {
    [for (x in [1, 2, 3]) x + 1]
}

pn main() {
    print(tune13_discard(2))
    print("\n")
    print(len(tune13_observed()))
    print("\n")
}
