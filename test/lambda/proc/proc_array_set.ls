// Test array indexed assignment in procedural code
// P0 feature for AWFY benchmarks

// Test 1: Basic integer array assignment
pn test_array_int_set() {
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

// Test 2: Negative indexing
pn test_array_negative_index() {
    var arr = [1, 2, 3, 4, 5]
    arr[-1] = 99
    arr[-3] = 77
    print(arr[2])
    print(" ")
    print(arr[4])
    print("\n")
}

// Test 3: Array assignment in loop
pn test_array_loop_set() {
    var arr = [0, 0, 0, 0, 0]
    var i = 0
    while (i < 5) {
        arr[i] = i * i
        i = i + 1
    }
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

// Test 4: Swap elements
pn test_array_swap() {
    var arr = [10, 20, 30]
    var tmp = arr[0]
    arr[0] = arr[2]
    arr[2] = tmp
    print(arr[0])
    print(" ")
    print(arr[1])
    print(" ")
    print(arr[2])
    print("\n")
}

pn main() {
    test_array_int_set()
    test_array_negative_index()
    test_array_loop_set()
    test_array_swap()
}
