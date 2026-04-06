// Test: mixing different sized numeric types in expressions
// Covers cross-type arithmetic, promotion, and comparison

// ===== Same-width mixed-sign arithmetic =====
"=== same-width mixed-sign ==="
42i8 + 10u8
1000i16 + 500u16
100i32 + 200u32

// ===== Cross-width sized arithmetic =====
"=== cross-width sized ==="
10i8 + 1000i16
5u8 + 100000i32
3i16 * 4u32

// ===== Sized + standard int/float =====
"=== sized + standard ==="
42i8 + 100
255u8 + 1000
100i32 + 3.14
50u16 * 2.5
10i8 + 100000000000i64

// ===== Sized + u64 =====
"=== sized + u64 ==="
1i8 + 100u64
100u64 + 42u32

// ===== Chained expressions =====
"=== chained ==="
10i8 * 20i16 + 5u32
100u8 - 10i8 + 3.14
(2i8 + 3i16) * (4u8 + 5u16)

// ===== Division producing float =====
"=== division ==="
10i8 / 3
7u16 / 2
100i32 / 3.0

// ===== Integer division =====
"=== integer division ==="
10i8 div 3
255u8 div 10
1000i16 div 7

// ===== Modulo =====
"=== modulo ==="
10i8 % 3
255u8 % 7
1000i16 % 13

// ===== Power =====
"=== power ==="
2i8 ** 8
3u8 ** 4
2i16 ** 10

// ===== Unary on mixed results =====
"=== unary ==="
-(42i8 + 8u8)
abs(-100i32 + 50i16)

// ===== Cross-type equality: sized vs sized =====
"=== equality ==="
42i8 == 42u8
10i16 == 10i32
100u32 == 100i8

// sized vs standard
42i8 == 42
255u8 == 255
100u64 == 100

// sized float vs sized float
3.14f32 == 3.14f32
1.5f16 == 1.5f16

// ===== Conversion across types =====
"=== conversions ==="
int(3.14f32)
float(42i8)
int(100u64)
float(100u64)
decimal(42i8)

// string conversions (each separated by non-string to avoid concat)
"=== string conv ==="
let s1 = string(255u8)
len(s1)
let s2 = string(100i32)
len(s2)
let s3 = string(1.5f16)
len(s3)
