// Test clock() system function
// clock() returns high-resolution monotonic time in seconds as a float

// Test 1: clock() returns a positive value
pn test_clock_positive() {
    let t = clock()
    if (t > 0.0) {
        print("positive")
    } else {
        print("ERROR: clock not positive")
    }
}

// Test 2: two consecutive clock() calls yield non-decreasing values
pn test_clock_monotonic() {
    let t0 = clock()
    let t1 = clock()
    if (t1 >= t0) {
        print("monotonic")
    } else {
        print("ERROR: clock not monotonic")
    }
}

// Test 3: clock() difference measures elapsed time (should be > 0 for a loop)
pn test_clock_elapsed() {
    let t0 = clock()
    var sum: int = 0
    var i: int = 0
    while (i < 10000) {
        sum = sum + i
        i = i + 1
    }
    let t1 = clock()
    let elapsed = t1 - t0
    if (elapsed >= 0.0) {
        print("elapsed_ok")
    } else {
        print("ERROR: negative elapsed")
    }
}

// Test 4: clock() can be used in arithmetic with float literals
pn test_clock_arithmetic() {
    let t0 = clock()
    let ms = t0 * 1000.0
    if (ms > 0.0) {
        print("arithmetic_ok")
    } else {
        print("ERROR: arithmetic failed")
    }
}

pn main() {
    test_clock_positive()
    print("\n")
    test_clock_monotonic()
    print("\n")
    test_clock_elapsed()
    print("\n")
    test_clock_arithmetic()
}
