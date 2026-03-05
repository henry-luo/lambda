// Test: Stats Advanced
// Layer: 2 | Category: function | Covers: variance, deviation, quantile
// math is a built-in module, accessed via math.method() syntax

// ===== variance =====
math.variance([2, 4, 4, 4, 5, 5, 7, 9])
math.variance([1, 1, 1, 1])
math.variance([1, 2, 3, 4, 5])

// ===== deviation (standard deviation) =====
math.deviation([2, 4, 4, 4, 5, 5, 7, 9])
math.deviation([1, 1, 1, 1])

// ===== quantile =====
math.quantile([1, 2, 3, 4, 5], 0.5)
math.quantile([1, 2, 3, 4, 5], 0.25)
math.quantile([1, 2, 3, 4, 5], 0.75)
math.quantile([1, 2, 3, 4, 5], 0.0)
math.quantile([1, 2, 3, 4, 5], 1.0)

// ===== Combined stats =====
let data = [10, 20, 30, 40, 50]
sum(data)
avg(data)
math.variance(data)
math.median(data)
