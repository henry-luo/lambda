// S4.1.2/D3.3.3v3: finite literal lanes have no post-allocation effects.
pn literal() int[] {
    return [0, 1, 2, 3]
}

pn dynamic(n: int) int[] {
    return [0, n, 2]
}

pn main() {
    print([literal(), dynamic(7)])
    print("\n")
}
