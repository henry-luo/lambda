// Test: int[] / int64[] / float[] parameter type annotations
// Regression test for ARRAY_INT inline fast path bug:
//   - get_effective_type returned LMD_TYPE_TYPE (annotation meta-type) instead
//     of the variable's tracked runtime type (ARRAY_INT), causing fast paths
//     to be skipped and fn_index to return <error>.
//   - ensure_typed_array was not called at param binding, so callers passing
//     generic Arrays would crash when FAST PATH did inline memory reads.

// ============================================================
// Test 1: int[] param — read elements
// ============================================================
pn test_int_array_read(arr: int[], n: int) {
    var i = 0
    while (i < n) {
        if (i > 0) { print(" ") }
        print(arr[i])
        i = i + 1
    }
    print("\n")
}

// ============================================================
// Test 2: int[] param — arithmetic on elements
// ============================================================
pn test_int_array_sum(arr: int[], n: int) {
    var sum = 0
    var i = 0
    while (i < n) {
        sum = sum + arr[i]
        i = i + 1
    }
    print(sum)
    print("\n")
}

// ============================================================
// Test 3: int[] param — write then read back
// ============================================================
pn test_int_array_write(arr: int[], n: int) {
    var i = 0
    while (i < n) {
        arr[i] = arr[i] * 10
        i = i + 1
    }
    // read back modified values
    i = 0
    while (i < n) {
        if (i > 0) { print(" ") }
        print(arr[i])
        i = i + 1
    }
    print("\n")
}

// ============================================================
// Test 4: int[] only (no native params) — pure annotation
// ============================================================
pn test_int_array_only(arr: int[]) {
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// ============================================================
// Test 5: float[] param — read and sum
// ============================================================
pn test_float_array_sum(arr: float[], n: int) {
    var sum = 0.0
    var i = 0
    while (i < n) {
        sum = sum + arr[i]
        i = i + 1
    }
    print(sum)
    print("\n")
}

// ============================================================
// Test 6: int[] from fill() — fill creates ArrayInt natively
// ============================================================
pn test_fill_to_int_param(arr: int[], n: int) {
    // fill(n, 0) creates ArrayInt, so ensure_typed_array is a no-op
    print(arr[0])
    print(" ")
    print(arr[n - 1])
    print("\n")
}

// ============================================================
// Test 7: Nested function calls with int[] params
// ============================================================
pn inner_sum(arr: int[], lo: int, hi: int) {
    var s = 0
    var i = lo
    while (i < hi) {
        s = s + arr[i]
        i = i + 1
    }
    return s
}

pn test_nested_int_array(arr: int[], n: int) {
    var left = inner_sum(arr, 0, n div 2)
    var right = inner_sum(arr, n div 2, n)
    print(left + right)
    print("\n")
}

// ============================================================
// Test 8: int[] param with comparison operators
// ============================================================
pn test_int_array_compare(arr: int[], n: int) {
    var max_val = arr[0]
    var i = 1
    while (i < n) {
        if (arr[i] > max_val) {
            max_val = arr[i]
        }
        i = i + 1
    }
    print(max_val)
    print("\n")
}

// ============================================================
// Main: exercise all test patterns
// ============================================================
pn main() {
    // Build arrays
    var a = fill(5, 0)
    a[0] = 10; a[1] = 20; a[2] = 30; a[3] = 40; a[4] = 50

    var b = fill(4, 0.0)
    b[0] = 1.5; b[1] = 2.5; b[2] = 3.5; b[3] = 4.5

    // Test 1: read
    test_int_array_read(a, 5)

    // Test 2: sum
    test_int_array_sum(a, 5)

    // Test 3: write+read (modifies array in-place)
    var c = fill(3, 0)
    c[0] = 1; c[1] = 2; c[2] = 3
    test_int_array_write(c, 3)

    // Test 4: int[] only annotation (no second native param)
    test_int_array_only(a)

    // Test 5: float[] sum
    test_float_array_sum(b, 4)

    // Test 6: fill creates ArrayInt; pass directly
    var d = fill(5, 0)
    d[0] = 99; d[4] = 77
    test_fill_to_int_param(d, 5)

    // Test 7: nested calls
    test_nested_int_array(a, 4)

    // Test 8: comparison (find max)
    test_int_array_compare(a, 5)
}
