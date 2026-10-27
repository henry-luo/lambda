// S4.4.4 / D2.4: only proven raw u64 operands may use native word operations.
fn lane(a: u64, b: u64) u64 => bxor(bor(a, b), band(a, b))
fn logical(a: u64) u64 => ushr(a, 11)
fn widths(a: u64) array => [ushr(a, 0), ushr(a, 1), ushr(a, 63), ushr(a, 64), ushr(a, 128)]
fn dynamic_shift(a: u64, count: int) u64 | error => ushr(a, count)
fn erased(a, b) => bxor(a, b)
fn optional(a: u64?) => if (a == null) null else bxor(a, 1u64)
let a = 0x8000000000000001u64
let b = 0x0102030405060708u64;
[lane(a, b) == 0x8102030405060709u64, logical(a) == 4503599627370496u64];
widths(a) == [a, 4611686018427387904u64, 1u64, 0u64, 0u64];
[erased(a, b) == 0x8102030405060709u64, dynamic_shift(a, 63) == 1u64,
 dynamic_shift(a, 64) == 0u64, dynamic_shift(a, -1) ^ { "negative" },
 optional(null) == null, optional(2u64) == 3u64]
