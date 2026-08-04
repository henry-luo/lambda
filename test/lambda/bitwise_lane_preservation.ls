// Bitwise ops preserve their operand's integer lane. v5 closes `int` at the
// exact int53 band, so shifts beyond it produce the shared signed infinity;
// i64 shifts retain their independent wide lane.

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

"=== int shift closes at the int53 boundary ==="
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
