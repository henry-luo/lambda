// A semantic int return may still be an Item until its call boundary.
// Passing that result to a native int parameter must unbox the Item first.
pn boxed_int_result(value: int) int {
    return int(value)
}

pn add_native(left: int, right: int) int {
    return left + right
}

pn main() {
    print([add_native(boxed_int_result(3903086636), 1),
        add_native(boxed_int_result(7), 5)])
}
