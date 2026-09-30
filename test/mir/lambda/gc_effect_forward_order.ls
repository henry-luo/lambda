// D5.3.2: the direct callee is declared after its caller.
pn gc_order_forward_caller(s: string, x: int) int {
    return gc_order_forward_leaf(x) + len(s)
}

pn gc_order_forward_leaf(x: int) int {
    var y = x
    while (y > 0) { y = y - 1 }
    return x + 1
}

pn main() { print(gc_order_forward_caller("abcd", 2)); print("\n") }
