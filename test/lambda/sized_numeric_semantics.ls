// Test refined sized scalar semantics: subtype checks, f64 spelling,
// Go-style runtime errors, shifts, conversions, and mixed sized-float promotion.

"=== subtype is ==="
1u8 is u8
1u8 is u16
1u8 is int
1u8 is float
1u8 is number
1u8 is integer
1u8 is decimal
1i64 is int
1i64 is float
1i64 is integer
1i64 is decimal
1.0f32 is float
1.0f32 is decimal
1.0f64 is f64
1.0f64 is float
1.0f64 is f32

"=== f64 type ==="
type(1.0f64)

"=== mixed arithmetic ==="
type(1.0f16 + 1.0f32)
type(1.0f32 + 1)

"=== div mod shift ==="
1u8 div 0u8
1u8 % 0u8
shl(1u8, -1)
shl(1, -1)
shl(1u8, 8)
type(shl(1u8, 8))

"=== conversion ==="
let x = -1
u8(x)
type(u8(x))
