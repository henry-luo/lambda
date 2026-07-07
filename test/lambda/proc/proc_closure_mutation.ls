// Phase 5: closures capture immutable snapshots. Read-only capture works;
// mutation of a captured binding is rejected by the error tests.

// Test 1: read-only capture from pn closure
pn test_proc_read_capture() {
    var base = 40
    pn add_two() {
        base + 2
    }
    let f = add_two
    f()
}

// Test 2: read-only capture from fn closure
pn test_read_only_capture() {
    var x = 42
    fn get_x(dummy) => x
    let f = get_x
    f(0)  // 42
}

// Test 3: capture snapshot is independent of later outer mutation
pn test_snapshot() {
    var x = 5
    fn get_x(dummy) => x
    let f = get_x
    x = 9
    f(0)
}

pn main() {
    print("T1:")
    print(test_proc_read_capture())
    print(" T2:")
    print(test_read_only_capture())
    print(" T3:")
    print(test_snapshot())
    "done"
}
