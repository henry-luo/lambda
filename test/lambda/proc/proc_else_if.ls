// Test else-if chains in procedural (pn) functions
// Validates that else if / else if / else works correctly in pn context

// Test 1: basic else if chain
pn test_basic_else_if() {
    var x = 2
    var result = ""
    if (x == 1) {
        result = "one"
    } else if (x == 2) {
        result = "two"
    } else if (x == 3) {
        result = "three"
    } else {
        result = "other"
    }
    result
}

// Test 2: else if with first branch taken
pn test_first_branch() {
    var v = 10
    var result = ""
    if (v > 5) {
        result = "big"
    } else if (v > 0) {
        result = "small"
    } else {
        result = "negative"
    }
    result
}

// Test 3: else if with last branch (else) taken
pn test_else_branch() {
    var v = -1
    var result = ""
    if (v > 5) {
        result = "big"
    } else if (v > 0) {
        result = "small"
    } else {
        result = "negative"
    }
    result
}

// Test 4: else if with middle branch taken
pn test_middle_branch() {
    var v = 3
    var result = ""
    if (v > 10) {
        result = "huge"
    } else if (v > 5) {
        result = "big"
    } else if (v > 0) {
        result = "positive"
    } else {
        result = "non-positive"
    }
    result
}

// Test 5: else if without final else
pn test_no_final_else() {
    var v = 5
    var result = "default"
    if (v == 1) {
        result = "one"
    } else if (v == 5) {
        result = "five"
    }
    result
}

// Test 6: else if in a while loop
pn test_else_if_in_loop() {
    let items = ["a", "bb", "ccc", "dddd"]
    var i = 0
    var result = items[0]  // initialize to avoid null
    // classify first item
    if (len(result) == 1) {
        result = "short"
    } else if (len(result) == 2) {
        result = "medium"
    } else if (len(result) == 3) {
        result = "long"
    } else {
        result = "extra"
    }
    i = 1
    while (i < len(items)) {
        let item = items[i]
        if (len(item) == 1) {
            result = result ++ " short"
        } else if (len(item) == 2) {
            result = result ++ " medium"
        } else if (len(item) == 3) {
            result = result ++ " long"
        } else {
            result = result ++ " extra"
        }
        i = i + 1
    }
    result
}

// Test 7: nested else if
pn test_nested_else_if() {
    var x = 2
    var y = 3
    var result = ""
    if (x == 1) {
        result = "x is 1"
    } else if (x == 2) {
        if (y == 1) {
            result = "x=2, y=1"
        } else if (y == 3) {
            result = "x=2, y=3"
        } else {
            result = "x=2, y=other"
        }
    } else {
        result = "x is other"
    }
    result
}

// Test 8: else if with string comparison
pn test_string_else_if() {
    let ext = ".cpp"
    var result = ""
    if (ext == ".c") {
        result = "c-file"
    } else if (ext == ".cpp") {
        result = "cpp-file"
    } else if (ext == ".h") {
        result = "header"
    } else {
        result = "other"
    }
    result
}

pn main() {
    print("T1:" ++ test_basic_else_if())
    print(" T2:" ++ test_first_branch())
    print(" T3:" ++ test_else_branch())
    print(" T4:" ++ test_middle_branch())
    print(" T5:" ++ test_no_final_else())
    print(" T6:" ++ test_else_if_in_loop())
    print(" T7:" ++ test_nested_else_if())
    print(" T8:" ++ test_string_else_if())
    "done"
}
