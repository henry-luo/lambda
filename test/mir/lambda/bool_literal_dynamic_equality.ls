// S5.1.1–S5.1.2: a dynamic Item compared with a bool literal needs only
// the bool tag and payload, including when the Item is null or an error.
pn equals_true(x: any) bool {
    return x == true
}

pn differs_false(x: any) bool {
    return false != x
}

pn both_dynamic(x: any, y: any) bool {
    return x == y
}

pn main() {
    print([equals_true(true), equals_true(false), equals_true(null),
        equals_true(1), equals_true("x"), equals_true(shl(1u32, -1))])
    print("\n")
    print([differs_false(true), differs_false(false), differs_false(null),
        differs_false(1), differs_false("x"), differs_false(shl(1u32, -1))])
    print("\n")
    print([both_dynamic(true, true), both_dynamic(true, 1),
        both_dynamic([1], [1])])
    print("\n")
}
