// Test: Take and Drop
// Layer: 3 | Category: function | Covers: take, drop

take([1, 2, 3, 4, 5], 3)
take([1, 2, 3], 5)
take([1, 2, 3], 0)
take([], 3)
drop([1, 2, 3, 4, 5], 2)
drop([1, 2, 3], 0)
drop([1, 2, 3], 5)
drop([], 2)
