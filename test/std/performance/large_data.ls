// Test: Large Data Processing
// Layer: 1 | Category: performance | Covers: large collections, pipeline

// Sum of range
sum(1 to 1000)

// Transform and filter large range
len([for (x in 1 to 100 where x % 3 == 0) x])

// Large array operations
let big = [for (x in 1 to 100) x]
sum(big)
len(big)
min(big)
max(big)
