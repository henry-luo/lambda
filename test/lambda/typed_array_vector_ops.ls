// Test: vectorized arithmetic with compact typed arrays (Phase 3)

// ===== f32 array arithmetic =====
"=== f32 arithmetic ==="
let a = [1.0f32, 2.0f32, 3.0f32]
let b = [10.0f32, 20.0f32, 30.0f32]
a + b
a - b
a * b
a / b
b + 1.0f32
b * 2.0f32
100.0f32 - b

// ===== i16 array arithmetic =====
"=== i16 arithmetic ==="
let c = [10i16, 20i16, 30i16]
let d = [1i16, 2i16, 3i16]
c + d
c - d
c * d
c + 5i16
2i16 * c

// ===== scalar broadcast with sized scalars =====
"=== sized scalar broadcast ==="
let e = [100, 200, 300]
e + 10i8
e * 2u16

// ===== i8 array arithmetic =====
"=== i8 arithmetic ==="
let f = [1i8, 2i8, 3i8]
let g = [10i8, 20i8, 30i8]
f + g
f * g

// ===== u8 array arithmetic =====
"=== u8 arithmetic ==="
let h = [10u8, 20u8, 30u8]
let j = [1u8, 2u8, 3u8]
h + j
h - j

// ===== division produces float =====
"=== division ==="
[10i16, 20i16, 30i16] / 3
[9.0f32, 16.0f32, 25.0f32] / [3.0f32, 4.0f32, 5.0f32]

// ===== math functions on typed arrays =====
"=== math functions ==="
math.prod([2i16, 3i16, 4i16])
math.cumsum([1i8, 2i8, 3i8, 4i8])
math.cumprod([1u8, 2u8, 3u8, 4u8])

// ===== mixed array + range =====
"=== array + range ==="
[10, 20, 30] + (1 to 3)

// ===== f32 power =====
"=== power ==="
[2.0f32, 3.0f32, 4.0f32] ** 2.0
