// Test cmd() system function in JIT mode
// Validates: 1-arg form, 2-arg form, error handling, trailing newline trimming,
//            non-zero exit code returns error, empty output success

// Test 1: basic 2-arg cmd with echo
pn test_echo() {
    var r = cmd("echo", "hello") or "fail"
    r
}

// Test 2: 1-arg cmd form (no args)
pn test_one_arg() {
    cmd("true") ^ { return "fail" }
    "ok"
}

// Test 3: trailing newline should be trimmed
pn test_newline_trim() {
    var r = cmd("echo", "trimmed") or "fail"
    r ++ "|"
}

// Test 4: non-zero exit code should return error
pn test_fail_error() {
    cmd("false") ^ { return "caught" }
    "missed"
}

// Test 5: command that writes to stderr and fails
pn test_bad_cmd() {
    cmd("ls", "/nonexistent_path_xyz_99") ^ { return "caught" }
    "missed"
}

// Test 6: multi-word output preserved
pn test_multi_word() {
    var r = cmd("echo", "one two three") or "fail"
    r
}

// Test 7: error propagation with ?
pn test_propagate() {
    var r = cmd("echo", "prop")^
    r
}

pn main() {
    print("T1:" ++ test_echo())
    print(" T2:" ++ test_one_arg())
    print(" T3:" ++ test_newline_trim())
    print(" T4:" ++ test_fail_error())
    print(" T5:" ++ test_bad_cmd())
    print(" T6:" ++ test_multi_word())
    print(" T7:" ++ test_propagate())
    "done"
}
