// Test: Stats Advanced
// Layer: 2 | Category: function | Covers: variance, deviation, quantile

// ===== variance =====
variance([2, 4, 4, 4, 5, 5, 7, 9])
variance([1, 1, 1, 1])
variance([1, 2, 3, 4, 5])

// ===== deviation (standard deviation) =====
deviation([2, 4, 4, 4, 5, 5, 7, 9])
deviation([1, 1, 1, 1])

// ===== quantile =====
quantile([1, 2, 3, 4, 5], 0.5)
quantile([1, 2, 3, 4, 5], 0.25)
quantile([1, 2, 3, 4, 5], 0.75)
quantile([1, 2, 3, 4, 5], 0.0)
quantile([1, 2, 3, 4, 5], 1.0)

// ===== Combined stats =====
let data = [10, 20, 30, 40, 50]
sum(data)
avg(data)
variance(data)
median(data)
