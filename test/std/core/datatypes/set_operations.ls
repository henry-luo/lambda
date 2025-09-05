// Test: Set Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Set literals (if supported, otherwise create from arrays)
let set1 = new Set([1, 2, 3, 4, 5])
let set2 = new Set([4, 5, 6, 7, 8])

// Basic operations
set1.add(6)         // Add element
let has = set1.has(3)  // true
let size = set1.size   // 6
set1.delete(1)       // Remove element

// Set operations
let union = new Set([...set1, ...set2])  // Union
let intersection = new Set([...set1].filter(x => set2.has(x)))  // Intersection
let difference = new Set([...set1].filter(x => !set2.has(x)))   // Difference

// Convert to array
let array_from_set = [...set1]  // [2, 3, 4, 5, 6]

// Final check
1
