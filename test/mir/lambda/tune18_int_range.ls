// Tune18 E5: a bounded container length keeps small arithmetic in-band.

pn range18(values: int[]) int {
    return len(values) + 1
}

pn range18_unproven(left: int, right: int) int {
    return left + right
}

pn main() {
    print(range18([1, 2, 3]))
    print("\n")
    print(range18_unproven(1, 2))
}
