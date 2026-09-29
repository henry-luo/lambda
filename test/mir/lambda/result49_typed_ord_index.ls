// An indexed ordinal keeps the string's Unicode and absent-read semantics.
pn typed_ord_index(value: string, index: int) int? {
    ord(value[index])
}

// The index expression can allocate after the source has been evaluated.
pn typed_ord_index_alloc(value: string, index: int) int? {
    ord(value[index + len([index]) - 1])
}

pn main() {
    print(typed_ord_index("A", 0)); print(" ")
    print(typed_ord_index("éx", 0)); print(" ")
    print(typed_ord_index("éx", 1)); print(" ")
    print(typed_ord_index("😀", 0)); print(" ")
    print(typed_ord_index("", 0)); print(" ")
    print(typed_ord_index("x", -1)); print(" ")
    print(typed_ord_index_alloc("Z", 0)); print("\n")
}
