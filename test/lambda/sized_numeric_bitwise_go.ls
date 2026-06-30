// Test Go-like fixed-width bitwise behavior for explicit sized integers.

"=== binary bitwise ==="
band(4294967295u32, 255u32)
type(band(4294967295u32, 255u32))
bor(256u32, 1u8)
type(bor(256u32, 1u8))
bxor(255u8, 15u8)
type(bxor(255u8, 15u8))

"=== unary bitwise ==="
bnot(0u32)
type(bnot(0u32))
bnot(0i8)
type(bnot(0i8))
bnot(-1i8)
type(bnot(-1i8))

"=== shifts ==="
shr(-1i32, 1)
type(shr(-1i32, 1))
shr(4294967295u32, 1)
type(shr(4294967295u32, 1))
shl(128u8, 1)
type(shl(128u8, 1))
shl(1i8, 7)
type(shl(1i8, 7))
shr(-128i8, 7)
type(shr(-128i8, 7))
shr(128u8, 7)
type(shr(128u8, 7))
shr(-128i8, 8)
type(shr(-128i8, 8))
shr(128u8, 8)
type(shr(128u8, 8))

"=== mixed compact/default ==="
band(4294967295u32, 255)
type(band(4294967295u32, 255))
band(255, 15u8)
type(band(255, 15u8))
band(-1i32, 4294967295u32)
type(band(-1i32, 4294967295u32))
shr(-1, 1)
type(shr(-1, 1))
shl(1, 64)
type(shl(1, 64))

"=== u64 ==="
bnot(0u64)
type(bnot(0u64))
shr(18446744073709551615u64, 1)
type(shr(18446744073709551615u64, 1))
