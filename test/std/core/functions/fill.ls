// Test: Fill
// Layer: 3 | Category: function | Covers: fill

fill(3, 0)
fill(5, 1)
fill(0, 42)
fill(3, "x")
fill(4, true)

// additional coverage
fill(3, null)
fill(3, "a")
fill(2, "hello world")
fill(1, "solo")
fill(1, true)
fill(1, null)
fill(0, "empty")
fill(0, null)
fill(3, 3.14)
fill(2, [1, 2])
