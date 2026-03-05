// Test: Math Min/Max
// Layer: 2 | Category: function | Covers: min, max with scalar and vector forms

// ===== Two-argument min =====
min(3, 5)
min(5, 3)
min(-1, 1)
min(0, 0)

// ===== Two-argument max =====
max(3, 5)
max(5, 3)
max(-1, 1)
max(0, 0)

// ===== Vector min =====
min([3, 1, 4, 1, 5])
min([100])
min([-3, -1, -5])

// ===== Vector max =====
max([3, 1, 4, 1, 5])
max([100])
max([-3, -1, -5])

// ===== Method-style =====
[3, 1, 4, 1, 5].min()
[3, 1, 4, 1, 5].max()

// ===== Float min/max =====
min(1.5, 2.5)
max(1.5, 2.5)
min([1.1, 2.2, 0.5])
max([1.1, 2.2, 0.5])
