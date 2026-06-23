// Views are read-only — mutation attempts are rejected, base is preserved.
// Phase 2b §2 of Lambda_Typed_Array2.md

pn test_view_mutation_rejected() {
    var arr = [10, 20, 30, 40, 50]
    var v = subview(arr, 1, 4)

    // attempt mutation through view — should be rejected (logged), view unchanged
    v[0] = 999

    // both the view and base should be unchanged
    print(v[0])     // 20
    print(" ")
    print(v[1])     // 30
    print(" ")
    print(v[2])     // 40
    print("\n")

    print(arr[1])   // 20 — base unchanged
    print(" ")
    print(arr[2])   // 30
    print("\n")
}

pn test_view_mutation_float() {
    var arr = [1.5, 2.5, 3.5, 4.5, 5.5]
    var v = subview(arr, 0, 3)

    v[1] = 99.9     // rejected

    print(v[0])     // 1.5
    print(" ")
    print(v[1])     // 2.5
    print(" ")
    print(v[2])     // 3.5
    print("\n")
}

pn test_base_mutation_visible_in_view() {
    // when base is mutated, the view's aliased data reflects the change
    var arr = [100, 200, 300, 400, 500]
    var v = subview(arr, 1, 4)

    arr[2] = 777    // mutates base

    print(v[0])     // 200
    print(" ")
    print(v[1])     // 777 — aliased
    print(" ")
    print(v[2])     // 400
    print("\n")
}

pn main() {
    test_view_mutation_rejected()
    test_view_mutation_float()
    test_base_mutation_visible_in_view()
}
