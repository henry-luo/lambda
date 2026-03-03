// Test multiple statements on one line separated by semicolons in pn context
// Verifies that ';' works as a statement terminator/separator
//
// Supported ';' usage:
//   - var_stam: var x = 0;
//   - assign_stam: x = val;
//   - break_stam: break;
//   - continue_stam: continue;
//   - return_stam: return val;
//   - raise_stam: raise val;
//   - _content_expr (expression statements): print("hi"); let a = 1;

// Test 1: multiple assignments on one line
pn test_multi_assign() {
    var a = 0
    var b = 0
    a = 5; b = 10
    a + b  // should be 15
}

// Test 2: assignment + return on one line
pn test_assign_return() {
    var x = 0
    x = 99; return x
    0  // should not reach here
}

// Test 3: print (expression) statements separated by semicolons
pn test_print_semicolon() {
    print("A"); print("B"); print("C")
    1
}

// Test 4: assignments with semicolons inside while body
pn test_while_semicolon() {
    var sum = 0
    var i = 0
    while (i < 5) {
        sum = sum + i; i = i + 1
    }
    sum  // should be 10 (0+1+2+3+4)
}

// Test 5: chained assignments with semicolons (fibonacci)
pn test_chained_assign() {
    var x = 1
    var y = 1
    var temp = 0
    var i = 0
    while (i < 5) {
        temp = x; x = y; y = temp + y; i = i + 1
    }
    x  // fibonacci: 1,1,2,3,5,8 -> x should be 8
}

// Test 6: if-else with semicolons inside blocks
pn test_if_else_semicolon() {
    var a = 0
    var b = 0
    let cond = true
    if (cond) {
        a = 10; b = 20
    }
    else {
        a = 30; b = 40
    }
    a + b  // should be 30
}

// Test 7: let bindings separated by semicolons (expressions)
pn test_let_semicolon() {
    let a = 100; let b = 200
    a + b  // should be 300
}

// Test 8: break with semicolon
pn test_break_semicolon() {
    var x = 0
    while (true) {
        x = x + 1; if (x == 3) { break; }
    }
    x  // should be 3
}

// Test 9: continue with semicolon
pn test_continue_semicolon() {
    var sum = 0
    var i = 0
    while (i < 6) {
        i = i + 1; if (i % 2 == 0) { continue; }
        sum = sum + i
    }
    sum  // should be 9 (1+3+5)
}

// Test 10: return with semicolon (early return)
pn test_return_semicolon() {
    return 42;
    0  // unreachable
}

// Test 11: two assignments chained on one line
pn test_double_assign() {
    var x = 0
    var y = 0
    x = 77; y = 88
    x + y  // should be 165
}

// Test 12: multiple function calls with semicolons
pn test_multi_call() {
    let a = string(10); let b = string(20)
    a ++ b  // should be "1020"
}

// Test 13: two var declarations on one line (now supported)
pn test_var_semicolon() {
    var x = 10; var y = 20
    x + y  // should be 30
}

// Test 14: var + assignment on one line
pn test_var_then_assign() {
    var x = 0; x = 42
    x  // should be 42
}

// Test 15: three var declarations on one line
pn test_three_vars() {
    var a = 1; var b = 2; var c = 3
    a + b + c  // should be 6
}

// main: run all tests and print results
pn main() {
    print("T1:"); print(test_multi_assign())
    print(" T2:"); print(test_assign_return())
    print(" T3:"); test_print_semicolon()
    print(" T4:"); print(test_while_semicolon())
    print(" T5:"); print(test_chained_assign())
    print(" T6:"); print(test_if_else_semicolon())
    print(" T7:"); print(test_let_semicolon())
    print(" T8:"); print(test_break_semicolon())
    print(" T9:"); print(test_continue_semicolon())
    print(" T10:"); print(test_return_semicolon())
    print(" T11:"); print(test_double_assign())
    print(" T12:"); print(test_multi_call())
    print(" T13:"); print(test_var_semicolon())
    print(" T14:"); print(test_var_then_assign())
    print(" T15:"); print(test_three_vars())
    "done"
}
