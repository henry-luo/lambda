// Test: Collection Construction
// Layer: 1 | Category: function | Covers: take, drop, zip, fill, range

// ===== take =====
take([1, 2, 3, 4, 5], 3)
take([1, 2, 3], 5)
take([], 3)

// ===== drop =====
drop([1, 2, 3, 4, 5], 2)
drop([1, 2, 3], 5)
drop([], 3)

// ===== zip =====
zip([1, 2, 3], ["a", "b", "c"])
zip([1, 2], ["a", "b", "c"])

// ===== fill =====
fill(5, 0)
fill(3, "x")

// ===== range function =====
range(1, 5)
range(0, 10, 2)
range(10, 0, -2)

// ===== Method-style =====
[1, 2, 3, 4, 5].take(3)
[1, 2, 3, 4, 5].drop(2)
