// Regression coverage for value-preserving u64 normalization in ordinary arithmetic.
let max_u64 = 18446744073709551615u64
let signed_edge = 9223372036854775807u64
let decimal_edge = 9223372036854775808u64
let top_tag_int64 = 288230376151711744i64

// Values above INT64_MAX promote to decimal instead of signed reinterpretation.
max_u64 * 1
max_u64 + 0.5
max_u64 div 2
max_u64 % 7

// The signed boundary remains exact and must not use the legacy error sentinel.
signed_edge + 0
signed_edge * 1
signed_edge + 0.5

// The first wide u64 value takes the decimal path without changing magnitude.
decimal_edge + 0
decimal_edge * 1
decimal_edge + 0.5

// Preserve already-correct fixed-width and comparison behavior.
max_u64 + 1u64
-max_u64
(max_u64 > 1)
(max_u64 < 0)
(-1 == max_u64)

// A raw signed value whose top byte matches the INT64 Item tag is still a value.
top_tag_int64
top_tag_int64 + 1i64

// Ordinary constructors also preserve full unsigned magnitude.
decimal(max_u64)
int(max_u64)
