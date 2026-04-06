// Test: compact typed arrays — Phase 2 of Typed Array unification
// Tests compact storage for i8[], u8[], i16[], u16[], i32[], u32[], f16[], f32[] arrays

// ===== i8 arrays =====
"=== i8 ==="
let a1 = [10i8, 20i8, 30i8]
a1
len(a1)
a1[0] + a1[1] + a1[2]

// ===== u8 arrays =====
"=== u8 ==="
let a2 = [100u8, 200u8, 255u8]
a2
len(a2)
a2[2]

// ===== i16 arrays =====
"=== i16 ==="
let a3 = [1000i16, 2000i16, -3000i16]
a3
len(a3)
a3[0] + a3[1]

// ===== u16 arrays =====
"=== u16 ==="
let a4 = [100u16, 200u16, 300u16]
a4
len(a4)
a4[1]

// ===== i32 arrays =====
"=== i32 ==="
let a5 = [100000i32, 200000i32, 300000i32]
a5
len(a5)
a5[0] + a5[2]

// ===== u32 arrays =====
"=== u32 ==="
let a6 = [1000000u32, 2000000u32]
a6
len(a6)
a6[0] + a6[1]

// ===== f32 arrays =====
"=== f32 ==="
let a7 = [1.5f32, 2.5f32, 3.5f32]
a7
len(a7)
a7[0] + a7[2]

// ===== f16 arrays =====
"=== f16 ==="
let a8 = [1.0f16, 2.0f16, 3.0f16]
a8
len(a8)
a8[1]

// ===== array ops on compact arrays =====
"=== ops ==="
// concat
[1i8, 2i8] ++ [3i8, 4i8]

// map
[10u16, 20u16, 30u16] | ~ * 2

// filter
[10i32, 20i32, 30i32, 40i32] that (~ > 25)

// ===== type annotations with compact arrays =====
"=== annotations ==="
let ta:u8[] = [1, 2, 3]
ta
len(ta)
ta[0]
