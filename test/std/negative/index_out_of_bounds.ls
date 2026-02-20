// Test: Index Out of Bounds
// Layer: 2 | Category: negative | Covers: array index out of range

let arr = [10, 20, 30]
arr[0]
arr[2]
arr[3]
arr[-1]
arr[100]

// String index
let s = "hello"
slice(s, 0, 1)
slice(s, 10, 20)
