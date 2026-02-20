// Test correlation calculation
let test_x = [0.8, 0.2, 0.6, 0.9, 0.3, 0.0, 0.7, 0.1]
let test_y = [0.3, 0.9, 0.5, 0.1, 0.7, 0.4, 0.8, 0.2]

let n = len(test_x)
let x_mean = avg(test_x)
let y_mean = avg(test_y)

// Calculate correlation components
let diffs_x = for (x in test_x) (x - x_mean)
let diffs_y = for (y in test_y) (y - y_mean)
let products = for (i in 0 to n-1) diffs_x[i] * diffs_y[i]
let numerator = sum(products)

let sq_diffs_x = for (x in test_x) (x - x_mean) ** 2
let sq_diffs_y = for (y in test_y) (y - y_mean) ** 2
let x_var = sum(sq_diffs_x)
let y_var = sum(sq_diffs_y)
let denominator = sqrt(x_var * y_var)

let result = if (denominator == 0.0) 0.0 else numerator / denominator

"Test data:"
test_x
test_y
"Statistics:"
x_mean
y_mean
"Correlation:"
result
