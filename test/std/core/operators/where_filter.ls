// Test: Where/That Filter
// Layer: 3 | Category: operator | Covers: that filtering

[1, 2, 3, 4, 5] that (~ > 3)
[1, 2, 3, 4, 5] that (~ % 2 == 0)
[1, 2, 3, 4, 5] that (~ == 3)
[1, 2, 3] that (~ > 10)
[1, 2, 3] that (~ > 0)
[1, 2, 3, 4, 5] | ~ * 2 that (~ > 5)
