// Test: VMap (Hash Map) - Procedural Tests
// Tests m.set() in-place mutation with various key types

pn main() {
    // Test 1: Basic set on empty map
    let m1 = map()
    m1.set("x", 10)
    m1.set("y", 20)
    print(m1.x)
    print(" ")
    print(m1.y)
    print(" ")
    print(len(m1))
    print("\n")

    // Test 2: Overwrite existing key
    let m2 = map(["color", "red"])
    m2.set("color", "blue")
    print(m2.color)
    print("\n")

    // Test 3: Set with integer keys
    let m3 = map([1, "one", 2, "two"])
    m3.set(3, "three")
    m3.set(4, "four")
    print(len(m3))
    print("\n")

    // Test 4: Integral numeric forms update one integer key
    let m4 = map()
    m4.set(1, "int")
    m4.set(1.0, "float")
    m4.set(1.00m, "decimal")
    m4.set(1n, "integer")
    print(len(m4))
    print(" ")
    print(m4[1])
    print("\n")

    // Test 5: String and symbol forms update one name key
    let m5 = map()
    m5.set("flag", "string")
    m5.set('flag', "symbol")
    print(len(m5))
    print(" ")
    print(m5.flag)
    print("\n")

    // Test 6: Mixed canonical key kinds via set
    let m6 = map()
    m6.set("name", "test")
    m6.set(42, "answer")
    m6.set(42.0m, "decimal answer")
    var invalid_key = null
    m6.set(3.5, "fractional") ^ { invalid_key = ^ }
    print(len(m6))
    print(" ")
    print(invalid_key is error)
    print("\n")

    // Test 7: Set then for-loop
    let m7 = map()
    m7.set("a", 1)
    m7.set("b", 2)
    m7.set("c", 3)
    print([for (k, v in m7) k ++ "=" ++ (v)])
    print("\n")

    // Test 8: Overwrite preserves other keys
    let m8 = map(["x", 1, "y", 2])
    m8.set("x", 99)
    print(m8.x)
    print(" ")
    print(m8.y)
    print("\n")

    // Test 9: Set nested map as value
    let inner = map(["val", 42])
    let m9 = map()
    m9.set("child", inner)
    print(m9.child.val)
    print("\n")

    // Test 10: Many sets in sequence
    let m10 = map()
    var i = 0
    while (i < 10) {
        m10.set(string(i), i * i)
        i = i + 1
    }
    print(len(m10))
    print("\n")

    // Test 11: a shared Lambda VMap detaches before mutation.
    let base = map(["value", 1])
    var changed = base
    changed.set("value", 99)
    print(base.value)
    print(" ")
    print(changed.value)
    print("\n")
}
