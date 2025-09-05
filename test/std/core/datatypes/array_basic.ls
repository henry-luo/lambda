// Test: Array Basic Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Array literals
let empty = []
let numbers = [1, 2, 3, 4, 5]
let mixed = [1, "two", 3.0, true, null]

// Accessing elements
let first = numbers[0]     // 1
let last = numbers[-1]     // 5
let slice = numbers[1:3]   // [2, 3]

// Array methods
let len = numbers.length()  // 5
let joined = numbers.join(",")  // "1,2,3,4,5"
let mapped = numbers.map(x => x * 2)  // [2, 4, 6, 8, 10]
let filtered = numbers.filter(x => x > 2)  // [3, 4, 5]
let reduced = numbers.reduce((acc, x) => acc + x, 0)  // 15

// Modifying arrays
numbers.push(6)       // [1, 2, 3, 4, 5, 6]
let popped = numbers.pop()  // 6
numbers.unshift(0)    // [0, 1, 2, 3, 4, 5]
let shifted = numbers.shift()  // 0

// Finding elements
let hasTwo = numbers.includes(2)  // true
let index = numbers.indexOf(3)    // 2
let found = numbers.find(x => x > 3)  // 4

// Final check
1
