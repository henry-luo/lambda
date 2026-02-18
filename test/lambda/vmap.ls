// Test: VMap (Hash Map) - Phase 1
// Tests map() constructor, map_set() immutable put, member access, for-loop, len

// Test 1: Basic construction and member access
let m1 = map("name", "Alice", "age", 30)
let t1 = m1.name
// expect: "Alice"

// Test 2: Integer value access
let t2 = m1.age
// expect: 30

// Test 3: len()
let t3 = len(m1)
// expect: 2

// Test 4: Empty map
let m0 = map()
let t4 = len(m0)
// expect: 0

// Test 5: Float values
let m2 = map("pi", 3.14, "e", 2.718)
let t5 = m2.pi
// expect: 3.14

// Test 6: map_set returns new map with added key
let m3 = map("x", 10)
let m4 = map_set(m3, "y", 20)
let t6 = m4.y
// expect: 20

// Test 7: map_set does not mutate original (copy-on-write)
let t7 = len(m3)
// expect: 1

// Test 8: New map from map_set preserves original keys
let t8 = m4.x
// expect: 10

// Test 9: map_set overwrites existing key in new copy
let m5 = map("color", "red")
let m6 = map_set(m5, "color", "blue")
let t9 = m5.color
// expect: "red" (original unchanged)

// Test 10: Overwritten value in new copy
let t10 = m6.color
// expect: "blue"

// Test 11: For-loop iteration with 'at' (k, v)
let m7 = map("a", 1, "b", 2, "c", 3)
let t11 = [for (k, v at m7) k ++ "=" ++ string(v)]
// expect: ["a=1", "b=2", "c=3"]

// Test 12: for-loop collect values
let t12 = [for (k, v at m7) v]
// expect: [1, 2, 3]

// Test 13: Multiple map_set chain
let base = map("a", 1)
let step1 = map_set(base, "b", 2)
let step2 = map_set(step1, "c", 3)
let t13 = len(step2)
// expect: 3

// Test 14: Original unchanged after chain
let t14 = len(base)
// expect: 1

// Test 15: Boolean values
let m8 = map("active", true, "deleted", false)
let t15 = m8.active
// expect: true

// Test 16: String values with special characters
let m9 = map("greeting", "hello world", "empty", "")
let t16 = m9.greeting
// expect: "hello world"

// Test 17: Nested construction - map as value
let inner = map("x", 42)
let outer = map("child", inner)
let t17 = outer.child.x
// expect: 42

// Test 18: Large map
let big = map("k1", 1, "k2", 2, "k3", 3, "k4", 4, "k5", 5)
let t18 = len(big)
// expect: 5

[t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15, t16, t17, t18]
