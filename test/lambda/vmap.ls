// Test: VMap (Hash Map) - Functional Tests
// Construction, member access, for-loop, len, non-string keys

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
let m3 = map(["active", true, "deleted", false])
[m3.active, m3.deleted]

// Test 7: String with spaces
let m4 = map(["greeting", "hello world"])
m4.greeting

// Test 8: Nested map as value
let inner = map(["x", 42])
let outer = map(["child", inner])
outer.child.x

// Test 9: For-loop with 'at' (k, v)
let m5 = map(["a", 1, "b", 2, "c", 3])
[for (k, v at m5) k ++ "=" ++ string(v)]

// Test 10: For-loop collect values
[for (k, v at m5) v]

// Test 11: For-loop single-variable (key only)
[for (k at m5) k]

// Test 12: Integer keys
let m6 = map([1, "one", 2, "two", 3, "three"])
len(m6)

// Test 13: Integer key for-loop
[for (k, v at m6) v]

// Test 14: Float keys
let m7 = map([3.14, "pi", 2.718, "e"])
len(m7)

// Test 15: Boolean keys
let m8 = map([true, "yes", false, "no"])
len(m8)

// Test 16: Bool key for-loop
[for (k, v at m8) v]

// Test 17: Large map
let big = map(["k1", 1, "k2", 2, "k3", 3, "k4", 4, "k5", 5])
len(big)

// Test 18: Mixed key types in construction
let m9 = map(["name", "test", 42, "answer", true, "flag"])
len(m9)

// Final result - collect key tests
[len(map()), m1.name, m1.age, len(m1), m2.pi, m3.active, m4.greeting, outer.child.x, len(m5), len(m6), len(m7), len(m8), len(big), len(m9)]
