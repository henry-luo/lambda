// Test: VMap (Hash Map) - Functional Tests
// Construction, member access, for-loop, len, canonical name/integer keys

// Test 1: Empty map
len(map())

// Test 2: map from array with string keys
let m1 = map(["name", "Alice", "age", 30])
m1.name

// Test 3: Integer value access
m1.age

// Test 4: len
len(m1)

// Test 5: Float values
let m2 = map(["pi", 3.14, "e", 2.718])
m2.pi

// Test 6: Boolean values in map
let m3 = map(["active", true, "deleted", false]);
[m3.active, m3.deleted]

// Test 7: String with spaces
let m4 = map(["greeting", "hello world"])
m4.greeting

// Test 8: Nested map as value
let inner = map(["x", 42])
let outer = map(["child", inner])
outer.child.x

// Test 9: For-loop with 'in' (k, v)
let m5 = map(["a", 1, "b", 2, "c", 3]);
[for (k, v in m5) k ++ "=" ++ (v)];

// Test 10: For-loop collect values
[for (k, v in m5) v];

// Test 11: For-loop single-variable (key only) - use two-var form
[for (k, v in m5) k]

// Test 12: Integer keys
let m6 = map([1, "one", 2, "two", 3, "three"])
len(m6);

// Test 13: Integer key for-loop
[for (k, v in m6) v]

// Test 14: Integral numeric forms are one integer key
let m7 = map([3, "int", 3.0, "float", 3.00m, "decimal", 3n, "integer"]);
[len(m7), m7[3], m7[3.0], m7[3.00m], m7[3n]]

// Test 15: String and symbol forms are one name key
let m8 = map(["flag", "string", 'flag', "symbol"]);
[len(m8), m8.flag, m8['flag']];

// Test 16: Name key for-loop
[for (k, v in m8) v]

// Test 17: Large map
let big = map(["k1", 1, "k2", 2, "k3", 3, "k4", 4, "k5", 5])
len(big)

// Test 18: Mixed canonical key kinds in construction
let m9 = map(["name", "test", 42, "answer"])
len(m9);

// Final result - collect key tests
[len(map()), m1.name, m1.age, len(m1), m2.pi, m3.active, m4.greeting, outer.child.x, len(m5), len(m6), len(m7), len(m8), len(big), len(m9)]
