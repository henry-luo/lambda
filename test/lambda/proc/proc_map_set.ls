// Test map field mutation in procedural code
// P1 feature for AWFY benchmarks

// Test 1: Basic field update
pn test_map_set_basic() {
    let obj = {x: 10, y: 20}
    obj.x = 42
    print(obj.x)
    print(" ")
    print(obj.y)
    print("\n")
}

// Test 2: Multiple field updates
pn test_map_set_multiple() {
    let obj = {a: 1, b: 2, c: 3}
    obj.a = 100
    obj.b = 200
    obj.c = 300
    print(obj.a)
    print(" ")
    print(obj.b)
    print(" ")
    print(obj.c)
    print("\n")
}

// Test 3: Update field in loop
pn test_map_set_loop() {
    let counter = {value: 0}
    var i = 0
    while (i < 5) {
        counter.value = counter.value + 1
        i = i + 1
    }
    print(counter.value)
    print("\n")
}

pn main() {
    test_map_set_basic()
    test_map_set_multiple()
    test_map_set_loop()
}
