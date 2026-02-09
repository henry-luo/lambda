// Test while-loop swap patterns (covers MIR JIT SSA destruction fix)
// These tests exercise three-way variable swap inside while loops,
// which triggered a lost-copy bug in MIR at optimization levels >= 2.

// Test 1: Fibonacci with three-way swap (the original bug case)
pn test_fib_basic() {
    var a: int = 0
    var b: int = 1
    var temp: int = 0
    var i: int = 2
    while (i <= 3) {
        temp = a + b
        a = b
        b = temp
        i = i + 1
    }
    b  // fib(3) = 2
}

// Test 2: Fibonacci(10) = 55
pn test_fib_10() {
    var a: int = 0
    var b: int = 1
    var temp: int = 0
    var i: int = 2
    while (i <= 10) {
        temp = a + b
        a = b
        b = temp
        i = i + 1
    }
    b
}

// Test 3: Fibonacci(20) = 6765
pn test_fib_20() {
    var a: int = 0
    var b: int = 1
    var temp: int = 0
    var i: int = 2
    while (i <= 20) {
        temp = a + b
        a = b
        b = temp
        i = i + 1
    }
    b
}

// Test 4: Bubble sort with swaps
pn test_bubble_sort() {
    var a: int = 5
    var b: int = 3
    var c: int = 1
    var d: int = 4
    var e: int = 2
    var temp: int = 0
    var swapped: int = 1
    while (swapped > 0) {
        swapped = 0
        if (a > b) { temp = a; a = b; b = temp; swapped = 1 }
        if (b > c) { temp = b; b = c; c = temp; swapped = 1 }
        if (c > d) { temp = c; c = d; d = temp; swapped = 1 }
        if (d > e) { temp = d; d = e; e = temp; swapped = 1 }
    }
    // sorted: 1 2 3 4 5 → encode as 12345
    a * 10000 + b * 1000 + c * 100 + d * 10 + e
}

// Test 5: GCD with modular swap (Euclidean algorithm)
pn test_gcd() {
    var x: int = 48
    var y: int = 18
    var temp: int = 0
    while (y > 0) {
        temp = y
        y = x % y
        x = temp
    }
    x  // gcd(48, 18) = 6
}

// Test 6: Collatz sequence step count
pn test_collatz() {
    var val: int = 27
    var steps: int = 0
    while (val > 1) {
        if (val % 2 == 0) {
            val = val / 2
        } else {
            val = val * 3 + 1
        }
        steps = steps + 1
    }
    steps  // collatz(27) = 111 steps
}

// Test 7: Two-variable swap without temp (rotation pattern)
pn test_rotate_swap() {
    var a: int = 10
    var b: int = 20
    var temp: int = 0
    var i: int = 0
    while (i < 3) {
        temp = a
        a = b
        b = temp
        i = i + 1
    }
    // after 3 swaps (odd count), a and b are swapped: a=20, b=10
    a * 100 + b  // 2010
}

// Test 8: Nested while with swap in inner loop
pn test_nested_swap() {
    var total: int = 0
    var i: int = 0
    while (i < 3) {
        var a: int = 1
        var b: int = 2
        var temp: int = 0
        var j: int = 0
        while (j < 2) {
            temp = a
            a = b
            b = temp
            j = j + 1
        }
        // after 2 swaps (even count), a=1, b=2 back to original
        total = total + a
        i = i + 1
    }
    total  // 1 + 1 + 1 = 3
}

// Test 9: Float swap in while loop
pn test_float_swap() {
    var a: float = 1.5
    var b: float = 2.5
    var temp: float = 0.0
    var i: int = 0
    while (i < 1) {
        temp = a + b
        a = b
        b = temp
        i = i + 1
    }
    // a=2.5, b=4.0 → a+b = 6.5 → as int = 6
    int(a + b)  // 6
}

// Test 10: Multiple swaps across different variable groups
pn test_multi_swap() {
    var x: int = 1
    var y: int = 2
    var z: int = 3
    var temp: int = 0
    var i: int = 0
    while (i < 1) {
        // rotate x → y → z → x
        temp = x
        x = y
        y = z
        z = temp
        i = i + 1
    }
    // x=2, y=3, z=1
    x * 100 + y * 10 + z  // 231
}

// Main procedure to run tests
pn main() {
    print("T1:")
    print(test_fib_basic())
    print(" T2:")
    print(test_fib_10())
    print(" T3:")
    print(test_fib_20())
    print(" T4:")
    print(test_bubble_sort())
    print(" T5:")
    print(test_gcd())
    print(" T6:")
    print(test_collatz())
    print(" T7:")
    print(test_rotate_swap())
    print(" T8:")
    print(test_nested_swap())
    print(" T9:")
    print(test_float_swap())
    print(" T10:")
    print(test_multi_swap())
    "done"
}
