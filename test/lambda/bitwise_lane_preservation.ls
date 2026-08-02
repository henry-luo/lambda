// Bitwise ops preserve their operand's integer lane, and an int shift stays
// `int` at every magnitude.
//
// C16 retired overflow promotion: there is no band for a shift to leave. That
// is also sound for shifts specifically — scaling by a power of two moves the
// exponent and leaves the mantissa alone, so a valid `int` shifts to a valid
// `int`. The values below print exactly for the same reason; before C16 they
// promoted to float and rendered lossily (2^62 as 4611686018427388000).
//
// History: before 2026-07-29 the inlined JIT path range-checked against the
// 56-bit payload width instead of +/-(2^53-1), so shl(1,54) minted a compact
// int holding a value the C path rejected.

"=== lane preserved by operand type ==="
type(band(12, 10))
type(bor(12, 3))
type(bxor(12, 10))
type(bnot(5))
type(shl(1, 2))
type(shr(8, 2))
type(band(12i64, 10i64))
type(shl(1i64, 54))
type(band(12u8, 10u8))
type(band(12i8, 10i8))

"=== int shift stays int past the old compact band ==="
shl(1, 52)
type(shl(1, 52))
shl(1, 53)
type(shl(1, 53))
shl(1, 54)
type(shl(1, 54))
shl(1, 62)
type(shl(1, 62))

"=== i64 shift keeps the wide lane, no promotion ==="
shl(1i64, 54)
type(shl(1i64, 54))

"=== shift edge cases ==="
shl(1, 64)
type(shl(1, 64))
shr(8, 2)
shr(-8, 2)

"=== plain values ==="
band(12, 10)
bor(12, 3)
bxor(12, 10)
bnot(5)
