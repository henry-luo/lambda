// Test array type conversion when assigning incompatible types
// Phase 3: Specialized arrays convert to generic on type mismatch

// Test 1: ArrayInt + float assignment → converts to generic Array
pn test_int_to_float() {
    var arr = [10, 20, 30]
    arr[1] = 3.14
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// Test 2: ArrayInt + string assignment → converts to generic Array
pn test_int_to_string() {
    var arr = [1, 2, 3]
    arr[0] = "hello"
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// Test 3: ArrayFloat + string assignment → converts to generic Array
pn test_float_to_string() {
    var arr = [1.1, 2.2, 3.3]
    arr[2] = "world"
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// Test 4: ArrayInt + null assignment → converts to generic Array
pn test_int_to_null() {
    var arr = [10, 20, 30]
    arr[1] = null
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// Test 5: ArrayFloat + int assignment → widened in place (no conversion)
pn test_float_accepts_int() {
    var arr = [1.5, 2.5, 3.5]
    arr[0] = 10
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// Test 6: ArrayInt same type — stays specialized (fast path)
pn test_int_same_type() {
    var arr = [10, 20, 30, 40, 50]
    arr[0] = 100
    arr[2] = 300
    arr[4] = 500
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

// Test 7: Mixed assignment sequence on same array
pn test_mixed_sequence() {
    var arr = [1, 2, 3]
    arr[0] = 3.14
    arr[1] = "hello"
    arr[2] = true
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

// Test 8: Large array conversion preserves all elements
pn test_large_array_conversion() {
    var arr = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    var i = 0
    while (i < 10) {
        arr[i] = i * 10
        i = i + 1
    }
    arr[5] = "mid"
    print(arr[0])
    print(" ")
    print(arr[3])
    print(" ")
    print(arr[5])
    print(" ")
    print(arr[9])
    print("\n")
}

pn main() {
    test_int_to_float()
    test_int_to_string()
    test_float_to_string()
    test_int_to_null()
    test_float_accepts_int()
    test_int_same_type()
    test_mixed_sequence()
    test_large_array_conversion()
}
