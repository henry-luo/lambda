// Test fill() built-in function and typed array coercion
// Covers: fill() return types, type annotation matches/mismatches,
//         mutation after coercion, cross-type conversion

// ============================================================
// Test 1: fill() with int value → ArrayInt (no annotation)
// ============================================================
pn test_fill_int_untyped() {
    var arr = fill(5, 0)
    arr[0] = 10
    arr[4] = 40
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[4])
    print("\n")
}

// ============================================================
// Test 2: fill() with float value → ArrayFloat (no annotation)
// ============================================================
pn test_fill_float_untyped() {
    var arr = fill(5, 0.0)
    arr[0] = 1.5
    arr[4] = 4.5
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[4])
    print("\n")
}

// ============================================================
// Test 3: fill() with bool → generic Array (no annotation)
// ============================================================
pn test_fill_bool_untyped() {
    var arr = fill(4, true)
    arr[1] = false
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print(" ")
    print(arr[3])
    print("\n")
}

// ============================================================
// Test 4: fill() with string → generic Array (no annotation)
// ============================================================
pn test_fill_string_untyped() {
    var arr = fill(3, "x")
    arr[1] = "y"
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// ============================================================
// Test 5: fill() with null → generic Array (no annotation)
// ============================================================
pn test_fill_null_untyped() {
    var arr = fill(3, null)
    arr[0] = 42
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// ============================================================
// Test 6: fill(0, val) → empty array
// ============================================================
pn test_fill_zero() {
    var arr = fill(0, 99)
    print(len(arr))
    print("\n")
}

// ============================================================
// Test 7: int[] = fill(n, int) → exact match, mutation works
// ============================================================
pn test_fill_int_typed_int() {
    var arr:int[] = fill(5, 0)
    arr[0] = 10
    arr[2] = 20
    arr[4] = 40
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print(" ")
    print(arr[3])
    print(" ")
    print(arr[4])
    print("\n")
}

// ============================================================
// Test 8: float[] = fill(n, float) → exact match, mutation works
// ============================================================
pn test_fill_float_typed_float() {
    var arr:float[] = fill(5, 0.0)
    arr[0] = 1.5
    arr[2] = 2.5
    arr[4] = 4.5
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print(" ")
    print(arr[3])
    print(" ")
    print(arr[4])
    print("\n")
}

// ============================================================
// Test 9: int[] = fill(n, float) → cross-type coercion
// ArrayFloat → ArrayInt, mutation works
// ============================================================
pn test_fill_float_typed_int() {
    var arr:int[] = fill(5, 0.0)
    arr[0] = 7
    arr[4] = 99
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[4])
    print("\n")
}

// ============================================================
// Test 10: float[] = fill(n, int) → cross-type coercion
// ArrayInt → ArrayFloat, mutation works
// ============================================================
pn test_fill_int_typed_float() {
    var arr:float[] = fill(5, 0)
    arr[0] = 3.14
    arr[4] = 2.72
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[4])
    print("\n")
}

// ============================================================
// Test 11: int[] = fill(n, bool) → generic Array → ArrayInt
// ============================================================
pn test_fill_bool_typed_int() {
    var arr:int[] = fill(3, true)
    arr[0] = 42
    arr[2] = 99
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// ============================================================
// Test 12: float[] = fill(n, bool) → generic Array → ArrayFloat
// ============================================================
pn test_fill_bool_typed_float() {
    var arr:float[] = fill(3, true)
    arr[0] = 1.5
    arr[2] = 9.9
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// ============================================================
// Test 13: fill() with non-zero int value, typed int[]
// ============================================================
pn test_fill_nonzero_int_typed() {
    var arr:int[] = fill(4, 7)
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print(" ")
    print(arr[3])
    arr[2] = 100
    print("\n")
    print(arr[2])
    print("\n")
}

// ============================================================
// Test 14: fill() with non-zero float value, typed float[]
// ============================================================
pn test_fill_nonzero_float_typed() {
    var arr:float[] = fill(4, 3.14)
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print(" ")
    print(arr[3])
    arr[1] = 99.9
    print("\n")
    print(arr[1])
    print("\n")
}

// ============================================================
// Test 15: fill() through wrapper function, typed int[]
// (simulates the make_array(n, val) pattern in benchmarks)
// ============================================================
pn make_array(n: int, val) {
    return fill(n, val)
}

pn test_fill_wrapper_typed_int() {
    var arr:int[] = make_array(100, 0)
    arr[0] = 42
    arr[50] = 77
    arr[99] = 88
    print(arr[0])
    print(" ")
    print(arr[50])
    print(" ")
    print(arr[99])
    print("\n")
}

// ============================================================
// Test 16: fill() through wrapper, typed float[]
// ============================================================
pn test_fill_wrapper_typed_float() {
    var arr:float[] = make_array(10, 0.0)
    arr[0] = 1.1
    arr[9] = 9.9
    print(arr[0])
    print(" ")
    print(arr[9])
    print("\n")
}

// ============================================================
// Test 17: fill() through wrapper cross-type: float[] = fill(n, int)
// ============================================================
pn test_fill_wrapper_cross_float() {
    var arr:float[] = make_array(5, 0)
    arr[0] = 2.5
    arr[4] = 8.8
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[4])
    print("\n")
}

// ============================================================
// Test 18: fill() through wrapper cross-type: int[] = fill(n, float)
// ============================================================
pn test_fill_wrapper_cross_int() {
    var arr:int[] = make_array(5, 0.0)
    arr[0] = 11
    arr[4] = 55
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[4])
    print("\n")
}

// ============================================================
// Test 19: Large fill + typed mutation (stress test)
// ============================================================
pn test_fill_large_typed() {
    var arr:int[] = fill(1000, 0)
    arr[0] = 1
    arr[499] = 500
    arr[999] = 1000
    print(arr[0])
    print(" ")
    print(arr[499])
    print(" ")
    print(arr[999])
    print("\n")
}

// ============================================================
// Test 20: fill() + loop mutation (typical benchmark pattern)
// ============================================================
pn test_fill_loop_mutation() {
    var arr:int[] = fill(10, 0)
    var i: int = 0
    while (i < 10) {
        arr[i] = i * i
        i = i + 1
    }
    print(arr[0])
    print(" ")
    print(arr[3])
    print(" ")
    print(arr[9])
    print("\n")
}

// ============================================================
// Test 21: fill() + negative int value, typed int[]
// ============================================================
pn test_fill_negative_int() {
    var arr:int[] = fill(3, -1)
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    arr[1] = 99
    print("\n")
    print(arr[1])
    print("\n")
}

// ============================================================
// Test 22: fill() + negative float value, typed float[]
// ============================================================
pn test_fill_negative_float() {
    var arr:float[] = fill(3, -1.5)
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    arr[2] = 0.0
    print("\n")
    print(arr[2])
    print("\n")
}

pn main() {
    test_fill_int_untyped()
    test_fill_float_untyped()
    test_fill_bool_untyped()
    test_fill_string_untyped()
    test_fill_null_untyped()
    test_fill_zero()
    test_fill_int_typed_int()
    test_fill_float_typed_float()
    test_fill_float_typed_int()
    test_fill_int_typed_float()
    test_fill_bool_typed_int()
    test_fill_bool_typed_float()
    test_fill_nonzero_int_typed()
    test_fill_nonzero_float_typed()
    test_fill_wrapper_typed_int()
    test_fill_wrapper_typed_float()
    test_fill_wrapper_cross_float()
    test_fill_wrapper_cross_int()
    test_fill_large_typed()
    test_fill_loop_mutation()
    test_fill_negative_int()
    test_fill_negative_float()
}
