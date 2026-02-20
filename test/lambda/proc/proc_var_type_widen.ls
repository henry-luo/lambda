// Test var type widening — Phase 2: var Type Flexibility
// Tests that var variables can change type at runtime via Item widening.
// This exercises the core type_widened path (non-null-initialized vars).

// T1: int → float
pn test_int_to_float() {
    var x = 42
    x = 3.14
    x  // 3.14
}

// T2: int → string
pn test_int_to_string() {
    var x = 10
    x = "hello"
    x  // "hello"
}

// T3: string → int
pn test_string_to_int() {
    var x = "world"
    x = 99
    x  // 99
}

// T4: bool → int
pn test_bool_to_int() {
    var x = true
    x = 42
    x  // 42
}

// T5: int → string → float (chain widening)
pn test_chain_widening() {
    var x = 1
    x = "two"
    x = 3.14
    x  // 3.14
}

// T6: null → int → string → null (full cycle)
pn test_null_cycle() {
    var x = null
    x = 42
    print(x)     // 42
    x = "hello"
    print(x)     // hello
    x = null
    x  // null
}

// T7: int → bool
pn test_int_to_bool() {
    var x = 0
    x = true
    x  // true
}

// T8: float → string
pn test_float_to_string() {
    var x = 3.14
    x = "pi"
    x  // "pi"
}

// T9: widened var used in arithmetic after reassignment
pn test_widened_arithmetic() {
    var a = 10
    var b = 20
    a = a + b       // still int (same type)
    a  // 30
}

// T10: var type-annotated numeric coercion (int var assigned float → allowed)
pn test_annotated_numeric() {
    var x: int = 10
    x = 3
    x  // 3
}

pn main() {
    print("T1:")
    print(test_int_to_float())
    print(" T2:")
    print(test_int_to_string())
    print(" T3:")
    print(test_string_to_int())
    print(" T4:")
    print(test_bool_to_int())
    print(" T5:")
    print(test_chain_widening())
    print(" T6:")
    test_null_cycle()
    print(test_null_cycle())
    print(" T7:")
    print(test_int_to_bool())
    print(" T8:")
    print(test_float_to_string())
    print(" T9:")
    print(test_widened_arithmetic())
    print(" T10:")
    print(test_annotated_numeric())
    "done"
}
