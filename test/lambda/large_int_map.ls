// Tests for large integers in maps and memory safety
// Covers: 56-bit integer storage, map field alignment, null value handling

"===== LARGE INTEGER IN MAP TESTS ====="

// Test 1: Large integers should not be truncated to 32-bit
"Test 1: Large integer literals in maps"
{x: 12300000000}
{value: 1.23e10}
{big: 9007199254740991}
{neg: -9007199254740991}

// Test 2: Scientific notation integers
"Test 2: Scientific notation integers"
{sci1: 1e10, sci2: 2e10, sci3: 5e9}
1.23e10
{a: 1.23e10, b: 2.34e10}

// Test 3: Integer boundary values
"Test 3: Integer boundary values (56-bit range)"
{max_safe: 9007199254740991, min_safe: -9007199254740991}
{very_large: 10000000000, also_large: 100000000000}

// Test 4: Maps with null values followed by other fields
// This tests the null field memory alignment fix
"Test 4: Maps with null values"
{a: 1, b: null, c: 2}
{x: null, y: 123, z: "test"}
{first: "hello", second: null, third: 456, fourth: null, fifth: 789}

// Test 5: Nested maps with null and large integers
"Test 5: Nested maps with mixed types"
{
    outer: {
        inner_int: 12300000000,
        inner_null: null,
        inner_str: "test"
    }
}

// Test 6: Maps with many fields including nulls
"Test 6: Maps with many fields"
{
    f1: 1, f2: 2, f3: null, f4: 4, f5: 5,
    f6: null, f7: 7, f8: 8, f9: null, f10: 10
}

// Test 7: Combination of primitives and strings sections (like test.json)
"Test 7: Primitives and strings together"
{
    primitives: {
        str: "Hello",
        number_integer: 42,
        number_float: 3.14159,
        number_negative: -123,
        number_zero: 0,
        number_scientific: 1.23e10,
        boolean_true: true,
        boolean_false: false,
        null_value: null
    },
    str_section: {
        empty_str: "",
        short: "abc",
        longer: "This is a longer string",
        another: "More text here",
        unicode: "Hello 世界",
        final: "end"
    }
}

// Test 8: Deep nesting with large integers
"Test 8: Deep nesting with large integers"
{
    level1: {
        level2: {
            level3: {
                big_value: 12300000000,
                small_value: 42
            }
        }
    }
}

// Test 9: Arrays containing large integers (via map)
"Test 9: Arrays with large integers"
{arr: [1e10, 2e10, 3e10]}

// Test 10: Field access on maps with large integers
"Test 10: Field access"
let m = {big: 12300000000, small: 42}
m.big
m.small
m.big + m.small

// Test 11: Arithmetic with large integers from maps
"Test 11: Arithmetic with map values"
let data = {a: 1e10, b: 2e10}
data.a + data.b
data.a * 2
data.b - data.a

// Test 12: Comparison with large integers
"Test 12: Comparisons"
12300000000 == 12300000000
(12300000000 > 12299999999)
1.23e10 == 12300000000

// Test 13: Many siblings in a map (stress test)
"Test 13: Many siblings"
{
    p1: 1, p2: 2, p3: 3, p4: 4, p5: 5,
    p6: 6, p7: 7, p8: 8, p9: 9, p10: 10,
    p11: 11, p12: 12, p13: 13, p14: 14, p15: 15,
    p16: null, p17: null, p18: null
}

"===== ALL LARGE INTEGER MAP TESTS COMPLETED ====="
