// Tune26 T26-3: a finite countdown with a zero-origin accumulator has a
// native arithmetic sibling; negative inputs retain the generic exit path.

pn tune26_descending_sum(n: int) int {
    var sum: int = 0
    while (n >= 0) {
        sum = sum + n
        n = n - 1
    }
    return sum
}

pn main() {
    print(tune26_descending_sum(4)); print(" ")
    print(tune26_descending_sum(-1)); print("\n")
}
