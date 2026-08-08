// Tune15 B2.1: a native-only function has no scalar home or fixed number
// scratch and therefore must not enter or restore an empty number frame.

pn tune15_leaf(value: int) int {
    return value + 1
}

pn main() {
    print(string(tune15_leaf(41)) ++ "\n")
}
