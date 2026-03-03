// Test: Map, Filter, Reduce via Pipe and That
// Layer: 3 | Category: function | Covers: pipe map, that filter, for reduce

// Map via pipe
[1, 2, 3] | ~ * 2
["hello", "world"] | len(~)

// Filter via that
[1, 2, 3, 4, 5] that (~ > 3)
[1, 2, 3, 4, 5, 6] that (~ % 2 == 0)

// Chain pipe and that
[1, 2, 3, 4, 5] | ~ * 2 that (~ > 5)
[1, 2, 3, 4, 5] that (~ > 2) | ~ * 10

// Reduce via sum
sum([1, 2, 3, 4, 5] | ~ * 2)

// For-based accumulation
sum([for (x in 1 to 10 where x % 2 == 0) x])
