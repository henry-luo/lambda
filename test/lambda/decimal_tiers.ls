// Suffix split (A.5): the suffix alone names the type — 'n' = integer always,
// 'm' = decimal always (all spellings, including integer-valued like 100m);
// fractional 'n' spellings are compile errors (see negative/semantic).
"=== suffix classification ==="
type(1n)
type(1e3n)
type(1.0m)
type(1e-3m)
type(100m)
1n == 1m

"=== integer arithmetic ==="
1n + 2n
type(1n + 2n)
5n - 7n
type(5n * 2n)

"=== division exits to decimal ==="
1n / 2n
type(1n / 2n)

"=== decimal precision ==="
0.1m == 0.10m
9999999999999999999999999999999999n
99999999999999999999999999999999999n
