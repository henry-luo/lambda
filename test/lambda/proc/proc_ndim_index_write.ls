// Multi-dim write: arr[i, j, k] = v
// Phase 2b §1 of Lambda_Typed_Array2.md — first-class multi-dim indexing

pn test_2d_write() {
    var m = [[1, 2, 3], [4, 5, 6]]

    // Single scalar writes
    m[0, 0] = 100
    m[1, 2] = 600
    print(m[0, 0])
    print(" ")
    print(m[1, 2])
    print("\n")

    // Verify other elements unchanged
    print(m[0, 1])    // 2
    print(" ")
    print(m[1, 0])    // 4
    print("\n")

    // Full row check
    print(m[0])       // [100, 2, 3]
    print(" ")
    print(m[1])       // [4, 5, 600]
    print("\n")
}

pn test_3d_write() {
    var t = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]]
    t[0, 0, 0] = 100
    t[1, 1, 1] = 800
    print(t[0, 0, 0])    // 100
    print(" ")
    print(t[1, 1, 1])    // 800
    print(" ")
    print(t[0, 1, 1])    // 4 — unchanged
    print(" ")
    print(t[1, 0, 1])    // 6 — unchanged
    print("\n")
}

pn test_negative_index_write() {
    var m = [[1, 2, 3], [4, 5, 6]]
    m[-1, -1] = 999
    m[-2, 0] = 111
    print(m[1, 2])       // 6 — negative writes are absent/no-op
    print(" ")
    print(m[0, 0])       // 1 — negative writes are absent/no-op
    print("\n")
}

pn test_float_write() {
    var f = [[1.5, 2.5], [3.5, 4.5]]
    f[0, 1] = 9.9
    f[1, 0] = -2.5
    print(f[0, 0])       // 1.5
    print(" ")
    print(f[0, 1])       // 9.9
    print(" ")
    print(f[1, 0])       // -2.5
    print(" ")
    print(f[1, 1])       // 4.5
    print("\n")
}

pn test_out_of_range_silent() {
    var m = [[1, 2], [3, 4]]
    m[5, 5] = 999        // out of range — no-op
    m[-99, 0] = 777      // also no-op
    print(m[0, 0])       // 1 (unchanged)
    print(" ")
    print(m[1, 1])       // 4 (unchanged)
    print("\n")
}

pn main() {
    test_2d_write()
    test_3d_write()
    test_negative_index_write()
    test_float_write()
    test_out_of_range_silent()
}
