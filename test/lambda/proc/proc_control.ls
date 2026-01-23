// Test procedural control flow features
// This file tests: while, var, assign, return, output

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
    x  // should not reach here
}

// Test 3: return with explicit value
pn test_return_value() {
    var x = 10
    return x * 2  // should return 20
    x  // should not reach here
}

// Test 4: nested while loops
pn test_nested_while() {
    var total = 0
    var i = 0
    while (i < 3) {
        var j = 0
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
    output(data, "/tmp/lambda_test_output.json")
    "json written"
}

// Test 7: output to file with explicit format
pn test_output_yaml() {
    let data = {items: [1, 2, 3], label: "numbers"}
    output(data, "/tmp/lambda_test_output.yaml", 'yaml')
    "yaml written"
}

// Main procedure to run tests
pn main() {
    print("Test 1 - simple while: ")
    print(test_while())
    print("; ")
    
    print("Test 2 - return in while: ")
    print(test_return())
    print("; ")
    
    print("Test 3 - return value: ")
    print(test_return_value())
    print("; ")
    
    print("Test 4 - nested while: ")
    print(test_nested_while())
    print("; ")
    
    print("Test 5 - multi var: ")
    print(test_multi_var())
    print("; ")
    
    print("Test 6 - output json: ")
    print(test_output_json())
    print("; ")
    
    print("Test 7 - output yaml: ")
    print(test_output_yaml())
    
    "All tests completed"
}
