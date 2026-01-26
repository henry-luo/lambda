// Array edge cases
// Tests empty arrays, large arrays, heterogeneous content, nesting

// Empty arrays
let empty1 = []
let empty2 = len([])
let empty3 = sum([])

// Single element arrays
let single1 = [42]
let single2 = [[1]]
let single3 = [[[1]]]

// Heterogeneous arrays
let mixed1 = [1, "two", 3.0, true, null]
let mixed2 = [1, [2, 3], {a: 4}, (x) => x + 5]

// Nested empty arrays
let nested_empty1 = [[], [[]], [[[]]]]
let nested_empty2 = [[[[]]]]

// Array operations on empty/small
let op1 = [][0]  // Should handle gracefully
let op2 = slice([], 0, 10)
let op3 = concat([], [])
let op4 = take([], 5)
let op5 = drop([1, 2], 10)

// Array index edge cases
let arr = [1, 2, 3]
let idx1 = arr[0]
let idx2 = arr[-1]  // Last element (if supported)
let idx3 = arr[100]  // Out of bounds

// Operations on single element
let single_ops1 = sum([42])
let single_ops2 = max([42])
let single_ops3 = min([42])
let single_ops4 = reverse([42])

// Large array (but reasonable for testing)
let large = range(0, 1000)
let large_ops1 = len(large)
let large_ops2 = sum(take(large, 10))

// Array with many nesting levels
let deep_nest = [[[[[[[[[[42]]]]]]]]]]

// Slicing edge cases
let slice1 = slice([1, 2, 3], 0, 0)  // Empty slice
let slice2 = slice([1, 2, 3], 1, 1)  // Empty slice
let slice3 = slice([1, 2, 3], -1, 10)  // Crosses boundaries

[len(empty1), len(single1), len(mixed1), len(large), deep_nest[0][0][0][0][0][0][0][0][0][0]]
