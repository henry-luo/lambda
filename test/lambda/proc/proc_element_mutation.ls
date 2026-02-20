// Test element mutation in procedural code (Phase 5)
// Verifies attribute assignment and child assignment on elements

// Test 1: Basic attribute update (same type)
pn test_attr_same_type() {
    let el = <div class: "old", id: "main">
    el.class = "new"
    print(el.class)
    print(" ")
    print(el.id)
    print("\n")
}

// Test 2: Attribute type change (int → string)
pn test_attr_type_change() {
    let el = <span count: 42, label: "ok">
    el.count = "many"
    print(el.count)
    print(" ")
    print(el.label)
    print("\n")
}

// Test 3: Child assignment with child elements
pn test_child_assign() {
    let el = <ul; <li; "A">; <li; "B">; <li; "C">>
    el[1] = <li; "X">
    print(el[0][0])
    print(" ")
    print(el[1][0])
    print(" ")
    print(el[2][0])
    print("\n")
}

// Test 4: Element attribute with null → container
pn test_attr_null_to_container() {
    let el = <div data: null, name: "box">
    el.data = [1, 2, 3]
    print(len(el.data))
    print(" ")
    print(el.name)
    print("\n")
}

// Test 5: Multiple attribute updates
pn test_multiple_attr_updates() {
    let el = <p x: 1, y: 2, z: 3>
    el.x = 10
    el.y = 20
    el.z = 30
    print(el.x)
    print(" ")
    print(el.y)
    print(" ")
    print(el.z)
    print("\n")
}

// Test 6: Attribute update preserves children
pn test_attr_preserves_children() {
    let el = <div class: "old"; <span; "inner">>
    el.class = "new"
    print(el.class)
    print(" ")
    print(el[0][0])
    print("\n")
}

// Test 7: Child update preserves attributes
pn test_child_preserves_attrs() {
    let el = <div id: "box"; "original content">
    el[0] = "updated"
    print(el.id)
    print(" ")
    print(el[0])
    print("\n")
}

// Test 8: Attribute mutation in loop
pn test_attr_in_loop() {
    let el = <span value: 0>
    var i = 0
    while (i < 5) {
        el.value = el.value + 1
        i = i + 1
    }
    print(el.value)
    print("\n")
}

pn main() {
    test_attr_same_type()
    test_attr_type_change()
    test_child_assign()
    test_attr_null_to_container()
    test_multiple_attr_updates()
    test_attr_preserves_children()
    test_child_preserves_attrs()
    test_attr_in_loop()
}
