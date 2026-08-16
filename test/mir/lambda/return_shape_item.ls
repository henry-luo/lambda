// Action B: a checked procedural body whose every explicit value exit is
// non-wide may use RETURN_SHAPE_ITEM; wide and unknown exits stay shape 2.

pn boxed_stable(value: int) string {
    // This checked declaration keeps the body on the boxed ABI while the
    // explicit string return remains provably wide-free.
    let checked: int = int(value)
    return "stable"
}

pn boxed_wide(value: int) {
    return int64(value)
}

pn boxed_unknown(value: any) any {
    return value
}

pn main() {
    print(boxed_stable(3) ++ "\n")
    print(string(boxed_wide(4)) ++ "\n")
    print(string(boxed_unknown(5)) ++ "\n")
}
