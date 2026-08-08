// Tune15 F1: keep a closed integer expression native until its float boundary.
pn tune15_integer_tree(i: int, j: int) float {
    return 1.0 / float((i + j) * (i + j + 1) / 2 + i + 1)
}

pn main() {
    print(tune15_integer_tree(3, 4) ++ "\n")
}
