// S2.5.2v2/S14.1.1v2: row lets precede filtering; windows follow ordering.
fn bounded_count(value: int) int {
    let temporary = [1, 2, 3]
    value
}
fn window(values: any, count: any) any { for (x in values limit count) x }
pn main() {
    print([for (x in [1, 2, 3], let doubled = x * 2, let shifted = doubled + 1 where shifted > 3) shifted]); print("\n")
    print([for (x in [3, 1, 4, 2], let square = x * x order by square desc limit 2 offset 1) x]); print("\n")
    print([for (x in 1 to 5 limit last 2) x]); print("\n")
    print([for (x in 1 to 5 offset 2) x]); print("\n")
    print([for (x in 1 to 5 limit 0) x]); print("\n")
    let one = for (x in 1 to 5 limit 1 offset 2) x
    print([one, one is int, window([1, 2], "bad") is error]); print("\n")
    let saved = [for (x in 1 to 3, let tiny = x * 1e-310 order by x desc limit bounded_count(2)) tiny]
    let churn = [for (x in 1 to 3, let tiny = x * 1e-300 order by x limit bounded_count(2)) tiny]
    print([saved[0] > 2e-310, saved[1] > 1e-310, len(saved) == 2, len(churn) == 2]); print("\n")
}
