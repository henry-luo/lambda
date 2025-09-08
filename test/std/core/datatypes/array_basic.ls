// Test: Array Basic Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Array literals
let empty = []
empty

let numbers = [1, 2, 3, 4, 5]
numbers

let mixed = [1, "two", 3.0, true, null]
mixed

let first = numbers[0]
first

let last = numbers[-1]
last
let slice = numbers[1:3]
slice

let len = numbers.length()
len

let joined = numbers.join(",")
joined
let mapped = numbers.map(x => x * 2)
mapped

let filtered = numbers.filter(x => x > 2)
filtered

let reduced = numbers.reduce((acc, x) => acc + x, 0)
reduced
numbers.push(6)
numbers
let popped = numbers.pop()
popped

numbers.unshift(0)
numbers

let shifted = numbers.shift()
shifted

numbers

let hasTwo = numbers.includes(2)
hasTwo

let index = numbers.indexOf(3)
index
let found = numbers.find(x => x > 3)
found
