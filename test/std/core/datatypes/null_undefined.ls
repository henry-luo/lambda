// Test: Null and Undefined Values
// Category: core/datatypes
// Type: positive
// Expected: 1

// Null and undefined values
let n = null
let u = undefined

// Comparison with null/undefined
let eq_null = n == null      // true
let eq_undefined = u == null // true (in non-strict comparison)
let strict_eq = n === null   // true (strict equality)
let strict_neq = n === u     // false (different types)

// Type checking
let is_null = n === null     // true
let is_undefined = u === undefined // true

// Default values with nullish coalescing
let default1 = n ?? "default"  // "default"
let default2 = u ?? 42         // 42
let default3 = 0 ?? "default"  // 0 (only null/undefined are replaced)

// Optional chaining
let obj = {a: {b: 42}}
let value1 = obj?.a?.b     // 42
let value2 = obj?.x?.y     // undefined (no error)

// Final check
1
