// Test: Truthy and Falsy Values
// Category: core/datatypes
// Type: positive
// Expected: 1

// Falsy values
let f1 = !!false      // false
let f2 = !!0          // false
let f3 = !!""         // false
let f4 = !!null       // false
let f5 = !!undefined  // false
let f6 = !!NaN        // false

// Truthy values
let t1 = !!true           // true
let t2 = !!1              // true
let t3 = !!-1             // true
let t4 = !!"hello"        // true
let t5 = !![]            // true
let t6 = !!{}            // true
let t7 = !![0]           // true
let t8 = !!function(){}  // true

// Conditional execution
let result1 = "" || "default"     // "default"
let result2 = 0 || 42            // 42
let result3 = null ?? "fallback"  // "fallback"

// Final check
1
