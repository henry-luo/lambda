// Test: Data Science
// Layer: 4 | Category: integration | Covers: vectors, stats, aggregation

// ===== Create dataset =====
let temperatures = [72.5, 68.3, 75.1, 80.2, 65.4, 71.8, 77.9, 69.0, 73.6, 82.1,
                    66.7, 74.3, 78.5, 70.2, 76.8, 81.0, 67.9, 73.1, 79.4, 64.8]

// ===== Basic stats =====
len(temperatures)
temperatures |> sum()
temperatures |> avg()
temperatures |> min()
temperatures |> max();

// ===== Range =====
(temperatures |> max()) - (temperatures |> min())

// ===== Sort and inspect =====
let sorted_temps = temperatures |> sort()
sorted_temps[0]
sorted_temps[19]

// ===== Quartile approximations =====
sorted_temps[4]
sorted_temps[9]
sorted_temps[14]

// ===== Filter hot days =====
let hot = temperatures |: ~ > 75.0
len(hot)

// ===== Filter cold days =====
let cold = temperatures |: ~ < 70.0
len(cold)

// ===== Normalize to 0-1 range =====
let min_t = temperatures |> min()
let max_t = temperatures |> max()
let range_t = max_t - min_t
let normalized = temperatures |> (~ - min_t) / range_t
normalized |> min()
normalized |> max()

// ===== Moving average (window of 3) =====
fn moving_avg(data, window: int) {
    for (i in 0 to len(data) - window)
        data |> slice(i, i + window) |> avg()
}
let ma = moving_avg(temperatures, 3)
len(ma)
ma[0]

// ===== Count by category =====
fn categorize(t) => if (t < 70.0) "cold" else if (t < 77.0) "mild" else "hot"
let categories = temperatures |> categorize(~)
let cold_count = categories |: ~ == "cold" |> len()
let mild_count = categories |: ~ == "mild" |> len()
let hot_count = categories |: ~ == "hot" |> len()
cold_count
mild_count
hot_count
cold_count + mild_count + hot_count

// ===== Squared deviations =====
let mean = temperatures |> avg()
let sq_devs = temperatures |> (~ - mean) ** 2
sq_devs |> avg()
