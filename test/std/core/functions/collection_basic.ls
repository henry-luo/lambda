// Test: Collection Basic
// Layer: 1 | Category: function | Covers: sort, reverse, unique, concat, slice

// ===== sort =====
sort([3, 1, 4, 1, 5])
sort([3, 1, 4, 1, 5], 'desc')
sort(["banana", "apple", "cherry"])

// ===== reverse =====
reverse([1, 2, 3])
reverse([])
reverse(["a", "b", "c"])

// ===== unique =====
unique([1, 2, 2, 3, 3, 3])
unique(["a", "b", "a", "c"])
unique([])

// ===== concat =====
concat([[1, 2], [3, 4], [5, 6]])

// ===== slice =====
slice([1, 2, 3, 4, 5], 1, 3)
slice([1, 2, 3, 4, 5], 2)
slice([1, 2, 3, 4, 5], 0, 2)

// ===== Method-style =====
[3, 1, 2].sort()
[1, 2, 3].reverse()
[1, 2, 2, 3].unique()
