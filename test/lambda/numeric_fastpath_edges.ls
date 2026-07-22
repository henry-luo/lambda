// Pins the observable numeric behavior that the M1 (exact compare) and M2
// (inline flex-int arithmetic) fast paths must preserve bit-for-bit.
// Both are representation changes only (P6), so every value AND every
// type() below must be identical before and after them.
//
// Two operand regimes are covered for comparisons, because they lower
// differently: statically-typed operands take native MIR compares, while
// `any`-typed operands (array element loads, untyped fn params) go through
// the runtime's exact comparison path.
fn show(x) => [type(x), x]

// `any`-typed comparison drivers: untyped params defeat static typing, so
// these exercise the runtime path rather than native MIR compares.
fn any_lt(a, b) => a < b
fn any_le(a, b) => a <= b
fn any_gt(a, b) => a > b
fn any_ge(a, b) => a >= b
fn any_eq(a, b) => a == b
fn any_ne(a, b) => a != b
fn all_ops(a, b) => [any_lt(a, b), any_le(a, b), any_gt(a, b), any_ge(a, b), any_eq(a, b), any_ne(a, b)]

"=== M2: int add/sub at the compact-int boundary ==="
show(9007199254740990 + 1)
show(9007199254740991 + 1)
show(9007199254740991 + 2)
show(-9007199254740990 - 1)
show(-9007199254740991 - 1)
show(-9007199254740991 - 2)
show(9007199254740991 - 1)
show(0 + 0)
show(0 - 0)

"=== M2: int mul at the compact-int boundary ==="
show(94906265 * 94906265)
show(94906266 * 94906266)
show(0 * -5)
show(-1 * 0)
show(3 * -7)
show(2147483647 * 2147483647)
show(-2147483647 * 2147483647)

"=== M2: int div is true division (float) ==="
show(1 / 3)
show(7 / 2)
show(-7 / 2)
show(0 / 5)
show(6 / 3)
show(9007199254740991 / 1)

"=== M2: idiv and mod stay integral ==="
show(7 div 2)
show(-7 div 2)
show(7 % 2)
show(-7 % 2)

"=== M1: float vs float, static ==="
show(0.5 > 0.25)
show(0.5 < 0.25)
show(-0.0 == 0.0)
show(-0.0 < 0.0)
show(0.1 + 0.2 == 0.3)
show(1e308 > 1e307)

"=== M1: float vs float, any-typed ==="
all_ops(0.5, 0.25)
all_ops(0.25, 0.5)
all_ops(0.5, 0.5)
all_ops(-0.0, 0.0)
all_ops(1e308, 1e307)
all_ops(-1e308, -1e307)

"=== M1: exactness above 2^53 — must NOT round through double ==="
all_ops(9007199254740992.0, 9007199254740993i64)
all_ops(9007199254740993i64, 9007199254740992.0)
all_ops(9007199254740992.0, 9007199254740992i64)
all_ops(9223372036854775807i64, 9.223372036854776e18)
all_ops(-9223372036854775807i64, -9.223372036854776e18)

"=== M1: float vs int, fractional parts ==="
all_ops(0.5, 0)
all_ops(0, 0.5)
all_ops(-0.5, 0)
all_ops(0, -0.5)
all_ops(0.3, 0)
all_ops(2.5, 2)
all_ops(2.5, 3)
all_ops(-2.5, -2)
all_ops(-2.5, -3)
all_ops(3.0, 3)

"=== M1: float vs u64 ==="
all_ops(18446744073709551615u64, 1.8446744073709552e19)
all_ops(1.8446744073709552e19, 18446744073709551615u64)
all_ops(0u64, -0.0)
all_ops(0u64, -0.5)
all_ops(0u64, 0.5)

"=== M1: infinities vs integers ==="
all_ops(1.0 / 0.0, 9223372036854775807i64)
all_ops(-1.0 / 0.0, -9223372036854775807i64)
all_ops(1.0 / 0.0, 18446744073709551615u64)
all_ops(1.0 / 0.0, 1.0 / 0.0)
all_ops(-1.0 / 0.0, 1.0 / 0.0)

// All four ordered operators must be false against NaN, per
// Lambda_Formal_Semantics.md 6.1 ("poison stays incomparable — nan < x false
// both ways, IEEE"). Regression guard: `le`/`ge` used to be derived by
// negating `gt`/`lt`, which reported true here because an unordered pair
// satisfies neither. The statically-typed path (native MIR compares) always
// answered false, so the two paths disagreed depending on inference.
"=== M1: NaN is unordered against everything ==="
all_ops(0.0 / 0.0, 0)
all_ops(0, 0.0 / 0.0)
all_ops(0.0 / 0.0, 0.0 / 0.0)
all_ops(0.0 / 0.0, 1.0 / 0.0)
all_ops(0.0 / 0.0, 0.5)

"=== M1: decimal operands keep the decimal relation ==="
all_ops(0.1m, 0.1)
all_ops(0.1m, 0)
all_ops(1m, 1)
all_ops(1m, 1.0)
all_ops(1n, 1)
all_ops(1n, 1.5)

"=== M1: signed/unsigned integer pairs ==="
all_ops(-1, 18446744073709551615u64)
all_ops(18446744073709551615u64, -1)
all_ops(9223372036854775807i64, 18446744073709551615u64)
all_ops(1i64, 1u64)

"=== M1: comparisons through array elements (any-typed loads) ==="
let fv = [0.5, 0.25, -0.0, 0.0]
[fv[0] > fv[1], fv[0] < fv[1], fv[2] == fv[3], fv[2] < fv[3]]
let mv = [9007199254740992.0, 9007199254740993i64]
[mv[0] < mv[1], mv[0] > mv[1], mv[0] == mv[1]]

"=== M1/M2: min/max and sort route through the same comparison ==="
min([0.5, 0.25, 2, -1])
max([0.5, 0.25, 2, -1])
min([9007199254740992.0, 9007199254740993i64])
max([9007199254740992.0, 9007199254740993i64])
sort([2, 0.5, -1, 0.25, 3])
sort([9007199254740993i64, 9007199254740992.0, 0])

"=== M2: accumulation loop crossing the boundary ==="
fn acc(n, step) => (
  for (i in 1 to n) i * step
)
show(sum(acc(10, 1)))
show(sum(acc(4, 2251799813685248)))

// The statically-typed operands below lower to native MIR compares, while the
// `all_ops` cases above go through the runtime. Both must agree.
"=== NaN: statically-typed operands take the native path ==="
fn static_ops(a: float, b: float) => [a < b, a <= b, a > b, a >= b, a == b, a != b]
static_ops(0.0 / 0.0, 1.0)
static_ops(1.0, 0.0 / 0.0)
static_ops(0.0 / 0.0, 0.0 / 0.0)
static_ops(0.5, 0.25)

// Elementwise keyword comparisons share the same scalar ordering, so a NaN
// lane must be false for every ordered operator here too.
"=== NaN: elementwise keyword comparisons ==="
let ew = [1.0, 0.0 / 0.0, 2.0]
ew lt 1.5
ew le 1.5
ew gt 1.5
ew ge 1.5
ew eq 1.0
ew ne 1.0
