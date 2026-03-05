// Test: Variadic Arguments
// Layer: 2 | Category: function | Covers: ... variadic params, varg()

// ===== Variadic function =====
fn sum_all(values...) => sum(values)
sum_all(1, 2, 3)
sum_all(10, 20, 30, 40)
sum_all(1)

// ===== varg() to access all args =====
fn count_args(args...) => len(args)
count_args(1, 2, 3)
count_args("a", "b")
count_args()

// ===== Variadic with leading params =====
fn prepend(prefix: string, items...) => items | prefix ++ string(~)
prepend("item_", 1, 2, 3)

// ===== Variadic forwarding =====
fn wrapper(args...) => sum_all(*args)
wrapper(1, 2, 3)
