// Tune15 B1.3: an unannotated fill(n, int) binding keeps its packed integer
// witness through an indexed native consumer.

pn tune15_fill_witness(n: int) int {
    var values = fill(n, 7)
    return values[0] + 1
}

pn main() {
    print(string(tune15_fill_witness(2)) ++ "\n")
}
