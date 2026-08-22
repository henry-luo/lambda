// Native i64/u64 lane arithmetic: `+ - *` on two wide operands lower to a
// single machine instruction instead of boxing through fn_add/fn_sub/fn_mul.
// The observable contract is LAMBDA_NUM_OVERFLOW_SIZED_WRAP — two's-complement
// wrap, identical bits for the signed and unsigned lane.

"=== i64 lane: value and type ==="
let a: i64 = 4611686018427387904i64
let b: i64 = 3i64;
[a + b, type(a + b)];
[a - b, type(a - b)];
[a * b, type(a * b)]

"=== i64 lane: wrap at the extremes ==="
let hi: i64 = 9223372036854775807i64
let lo: i64 = -9223372036854775808i64
hi + 1i64
hi * 2i64
lo - 1i64
lo * -1i64
lo + lo

"=== i64 lane: nested trees keep the lane ===";
(a + b) * 2i64
a * b - a * b;
(hi - 1i64) + (1i64 - hi)

"=== u64 lane ==="
let u: u64 = 18446744073709551615u64
let v: u64 = 2u64;
[u + v, type(u + v)];
[u * v, type(u * v)];
[u - v, type(u - v)]

"=== mixed signed/unsigned joins in the u64 lane ==="
let s: i64 = -1i64;
[s * u, type(s * u)];
[s + u, type(s + u)]

"=== literal operands ==="
9223372036854775807i64 + 9223372036854775807i64
9223372036854775807i64 * 3i64

"=== off-lane joins are unchanged ===";
// i64 against `int` widens to the exact integer tower, not the wrapping lane
[a + 1, type(a + 1)];
[int64(2000000000000) - 100, type(int64(2000000000000) - 100)];
// i64 against a binary float joins in the float lane
[2i64 * 1.5, type(2i64 * 1.5)]
// a narrow sized operand is a packed Item, not a wide lane
let n: i32 = 7i32;
[a + n, type(a + n)];
// div/mod never enter the sized lane
[a div b, type(a div b)];
[a % b, type(a % b)]

"=== end ==="
