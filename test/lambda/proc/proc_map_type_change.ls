// Test map field type-changing assignments (Phase 4)
// Verifies shape rebuild when field types change at runtime

// Test 1: int → string
pn test_int_to_string() {
    let obj = {x: 10, y: 20}
    obj.x = "hello"
    print(obj.x)
    print(" ")
    print(obj.y)
    print("\n")
}

// Test 2: int → float
pn test_int_to_float() {
    let obj = {val: 42}
    obj.val = 3.14
    print(obj.val)
    print("\n")
}

// Test 3: string → int
pn test_string_to_int() {
    let obj = {name: "alice", age: 30}
    obj.name = 999
    print(obj.name)
    print(" ")
    print(obj.age)
    print("\n")
}

// Test 4: null → container (array)
pn test_null_to_array() {
    let obj = {data: null, tag: "ok"}
    obj.data = [1, 2, 3]
    print(len(obj.data))
    print(" ")
    print(obj.tag)
    print("\n")
}

// Test 5: container → null
pn test_array_to_null() {
    let obj = {items: [10, 20], count: 2}
    obj.items = null
    print(obj.items)
    print(" ")
    print(obj.count)
    print("\n")
}

// Test 6: multiple type changes on same map
pn test_multiple_type_changes() {
    let obj = {val: 1}
    print(obj.val)
    obj.val = "two"
    print(" ")
    print(obj.val)
    obj.val = 3.0
    print(" ")
    print(obj.val)
    print("\n")
}

// Test 7: type change preserves other fields
pn test_preserves_fields() {
    let obj = {a: 1, b: "hello", c: true}
    obj.b = 42
    print(obj.a)
    print(" ")
    print(obj.b)
    print(" ")
    print(obj.c)
    print("\n")
}

// Test 8: type change in loop
pn test_type_change_in_loop() {
    let obj = {state: 0}
    var i = 0
    while (i < 3) {
        if (i == 1) {
            obj.state = "active"
        } else {
            obj.state = i
        }
        i = i + 1
    }
    print(obj.state)
    print("\n")
}

pn main() {
    test_int_to_string()
    test_int_to_float()
    test_string_to_int()
    test_null_to_array()
    test_array_to_null()
    test_multiple_type_changes()
    test_preserves_fields()
    test_type_change_in_loop()
}
