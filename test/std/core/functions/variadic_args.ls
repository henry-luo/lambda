// Test: Variadic Arguments
// Layer: 2 | Category: function | Covers: ... variadic params, varg(), call()
// The variadic parameter is spelled `(...)`; arguments are read with varg().

// ===== Variadic function =====
fn sum_all(...) => sum(varg())
sum_all(1, 2, 3)
sum_all(10, 20, 30, 40)
sum_all(1)

// ===== varg() to access all args =====
// NOTE: only numeric arguments here — varg() currently merges adjacent
// string arguments and strips nulls (LR09-9), so a string count would
// pin buggy behaviour as expected output.
fn count_args(...) => len(varg())
count_args(1, 2, 3)
count_args(7, 8)
count_args()

// ===== Variadic with leading params =====
fn prepend(prefix: string, ...) => varg() |> prefix ++ (~)
prepend("item_", 1, 2, 3)

// ===== Variadic forwarding — S12.3.4 call(), not spread (S12.3.5) =====
fn wrapper(...) => call(sum_all, varg())
wrapper(1, 2, 3)
