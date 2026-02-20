// Test procedural control flow features
// This file tests: while, var, assign, return, output, if, break, continue

// Test 1: simple while loop
pn test_while() {
    var x = 0
    while (x < 5) {
        x = x + 1
    }
    x  // should be 5
}

// Test 2: while with early return
pn test_return() {
    var x = 0
    while (x < 100) {
        x = x + 1
        return x  // early return at x=1
    }
    x
}

// Test 3: return with explicit value
pn test_return_value() {
    var x = 10
    return x * 2  // should return 20
}

// Test 4: nested while loops
pn test_nested_while() {
    var total = 0
    var i = 0
    var j = 0
    while (i < 3) {
        j = 0
        while (j < 3) {
            total = total + 1
            j = j + 1
        }
        i = i + 1
    }
    total  // should be 9 (3x3)
}

// Test 5: multiple var declarations
pn test_multi_var() {
    var a = 1
    var b = 2
    var c = 3
    a + b + c  // should be 6
}

// Test 6: output to file (json auto-detect)
pn test_output_json() {
    let data = {name: "test", value: 42}
    output(data, "/tmp/lambda_test_output.json")^
    1  // success
}

// Test 7: output to file with explicit format
pn test_output_yaml() {
    let data = {items: [1, 2, 3], label: "numbers"}
    output(data, "/tmp/lambda_test_output.yaml", 'yaml')^
    1  // success
}

// Test 8: break inside if
pn test_break() {
    var x = 0
    while (x < 100) {
        x = x + 1
        if (x == 3) {
            break
        }
    }
    x  // should be 3
}

// Test 9: continue inside if
pn test_continue() {
    var sum = 0
    var i = 0
    while (i < 10) {
        i = i + 1
        if (i % 2 == 0) {
            continue
        }
        sum = sum + i  // sum odd numbers: 1+3+5+7+9 = 25
    }
    sum
}

// Test 10: if/else statement
pn test_if_else() {
    var x = 5
    var result = 0
    if (x > 3) {
        result = 1
    } else {
        result = 2
    }
    result  // should be 1
}

// Test 11: nested if
pn test_nested_if() {
    var x = 10
    var result = 0
    if (x > 5) {
        if (x > 8) {
            result = 3
        } else {
            result = 2
        }
    } else {
        result = 1
    }
    result  // should be 3
}

// Main procedure to run tests
pn main() {
    print("T1:")
    print(test_while())
    print(" T2:")
    print(test_return())
    print(" T3:")
    print(test_return_value())
    print(" T4:")
    print(test_nested_while())
    print(" T5:")
    print(test_multi_var())
    print(" T6:")
    print(test_output_json())
    print(" T7:")
    print(test_output_yaml())
    print(" T8:")
    print(test_break())
    print(" T9:")
    print(test_continue())
    print(" T10:")
    print(test_if_else())
    print(" T11:")
    print(test_nested_if())
    "done"
}
