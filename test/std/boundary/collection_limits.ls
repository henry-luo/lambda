// Test: Collection Limits
// Layer: 2 | Category: boundary | Covers: empty collections, nesting

// Empty collections
len([])
len({})
sum([])

// Single element operations
[42][0]
len([42])
sort([42])
reverse([42])

// Nested arrays
[[1, 2], [3, 4]]
[[1, 2], [3, 4]][0]
[[1, 2], [3, 4]][1]

// Nested maps
{a: {x: 1, y: 2}, b: {x: 3, y: 4}}
let m = {a: {x: 1, y: 2}}; m.a.x
