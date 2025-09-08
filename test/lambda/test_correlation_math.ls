// Test correlation calculation specifically
pub fn correlation(x_vals: [float], y_vals: [float]) {
    if len(x_vals) != len(y_vals) { null }
    
    let n = len(x_vals);
    let x_mean = avg(x_vals);
    let y_mean = avg(y_vals);
    
    let numerator = sum(for (i in 0 to n-1) (x_vals[i] - x_mean) * (y_vals[i] - y_mean));
    let x_var = sum(for (x in x_vals) (x - x_mean) ^ 2);
    let y_var = sum(for (y in y_vals) (y - y_mean) ^ 2);
    let denominator = (x_var * y_var) ^ 0.5;
    
    if (denominator == 0.0) 0.0 else numerator / denominator
}

let test_x = [0.8, 0.2, 0.6, 0.9, 0.3, 0.0, 0.7, 0.1];
let test_y = [0.3, 0.9, 0.5, 0.1, 0.7, 0.4, 0.8, 0.2];

let result = correlation(test_x, test_y);
"correlation result: " + string(result)
