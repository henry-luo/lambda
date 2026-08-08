// Tune15 B2.2: statically-bound scalar pn calls pass native lanes directly
// between declared procedures.

pn tune15_add(left: int, right: int) int {
    return left + right
}

pn tune15_call(value: int) int {
    return tune15_add(value, 1) + tune15_add(value, 2)
}

pn main() {
    print(string(tune15_call(20)) ++ "\n")
}
