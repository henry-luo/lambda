// Test procedural var null assignment and if-else with multiple statements
// This file tests fixes for:
// 1. var x = null should produce Item type (not void*)
// 2. Variables initialized to null can be reassigned and printed
// 3. if-else blocks with multiple statements work correctly

// Test 1: var initialized to null, then reassigned to int
pn test_var_null_int() {
    var x = null
    x = 42
    x  // should be 42
}

// Test 2: var initialized to null, then reassigned to string
pn test_var_null_string() {
    var x = null
    x = "hello"
    x  // should be "hello"
}

// Test 3: var initialized to null, print it
pn test_print_null() {
    var x = null
    print(x)  // should print "null"
    1
}

// Test 4: var null reassigned then printed
pn test_print_reassigned() {
    var x = null
    x = 99
    print(x)  // should print 99
    1
}

// Test 5: if-else with multiple statements in else block
pn test_if_else_multi_else() {
    var config = 10
    var result = 0
    var msg = null
    
    if (config == null) {
        result = -1
    }
    else {
        msg = "loaded"
        result = config * 2
    }
    result  // should be 20
}

// Test 6: if-else with multiple statements in both branches
pn test_if_else_multi_both() {
    var x = 5
    var a = 0
    var b = 0
    
    if (x > 10) {
        a = 1
        b = 2
    }
    else {
        a = 3
        b = 4
    }
    a + b  // should be 7 (3+4)
}

// Test 7: nested if with multiple statements
pn test_nested_if_multi() {
    var x = 15
    var r1 = 0
    var r2 = 0
    
    if (x > 10) {
        r1 = 1
        if (x > 12) {
            r2 = 2
        }
        else {
            r2 = 3
        }
    }
    r1 + r2  // should be 3 (1+2)
}

// Test 8: var null with member access after reassignment
pn test_var_null_member() {
    var obj = null
    obj = {name: "test", value: 100}
    obj.value  // should be 100
}

// Test 9: multiple var nulls with reassignment
pn test_multi_var_null() {
    var a = null
    var b = null
    var c = null
    a = 1
    b = 2
    c = 3
    a + b + c  // should be 6
}

// Test 10: if-else with assignment to null-initialized var
pn test_if_else_assign_null_var() {
    var x = null
    var flag = true
    
    if (flag) {
        x = "yes"
    }
    else {
        x = "no"
    }
    x  // should be "yes"
}

// Test 11: var null in while loop
pn test_var_null_while() {
    var result = null
    var i = 0
    while (i < 3) {
        result = i * 10
        i = i + 1
    }
    result  // should be 20 (last iteration: 2*10)
}

// Test 12: print var after conditional assignment
pn test_print_conditional() {
    var msg = null
    var x = 5
    
    if (x > 3) {
        msg = "big"
    }
    else {
        msg = "small"
    }
    print(msg)  // should print "big"
    1
}

// Main procedure to run tests
pn main() {
    print("T1:")
    print(test_var_null_int())
    print(" T2:")
    print(test_var_null_string())
    print(" T3:")
    test_print_null()
    print(" T4:")
    test_print_reassigned()
    print(" T5:")
    print(test_if_else_multi_else())
    print(" T6:")
    print(test_if_else_multi_both())
    print(" T7:")
    print(test_nested_if_multi())
    print(" T8:")
    print(test_var_null_member())
    print(" T9:")
    print(test_multi_var_null())
    print(" T10:")
    print(test_if_else_assign_null_var())
    print(" T11:")
    print(test_var_null_while())
    print(" T12:")
    test_print_conditional()
    "done"
}
