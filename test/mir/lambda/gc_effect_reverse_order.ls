// D5.3.2: the same call graph, with its callee declared first.
pn gc_order_reverse_leaf(x: int) int {
    var y = x
    while (y > 0) { y = y - 1 }
    return x + 1
}

pn gc_order_reverse_caller(s: string, x: int) int {
    return gc_order_reverse_leaf(x) + len(s)
}

pn main() { print(gc_order_reverse_caller("abcd", 2)); print("\n") }
