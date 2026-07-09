// Unified n-suffix semantics: integer-like spellings become integer;
// fractional or negative-exponent spellings remain decimal.
"=== n suffix classification ==="
type(1n)
type(1e3n)
type(1.0n)
type(1e-3n)

"=== integer arithmetic ==="
1n + 2n
type(1n + 2n)
5n - 7n
type(5n * 2n)

"=== division exits to decimal ==="
1n / 2n
type(1n / 2n)

"=== decimal precision ==="
0.1n == 0.10n
9999999999999999999999999999999999n
99999999999999999999999999999999999n
