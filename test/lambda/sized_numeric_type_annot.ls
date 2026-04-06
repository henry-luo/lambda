// Test: type annotations with sized numeric types
// Covers variable annotations, fn param types, return types, is-checks

// ===== Variable type annotations =====
"=== var annotations ==="
let a: i8 = 42i8
a
let b: u16 = 500u16
b
let c: i32 = 99999i32
c
let d: u64 = 1000u64
d
let e: f32 = 2.5f32
e

// ===== type() on annotated variables =====
"=== type checks ==="
type(a)
type(b)
type(c)
type(d)
type(e)

// ===== is-checks for specific sized types =====
"=== is checks ==="
42i8 is i8
500u16 is u16
99999i32 is i32
1000u64 is u64
2.5f32 is f32
1.5f16 is f16
255u8 is u8
1000i16 is i16

// ===== is-check: sized types are also number =====
"=== is number ==="
42i8 is number
500u16 is number
1000u64 is number
2.5f32 is number

// ===== is-check: negative (wrong sub-type) =====
"=== is negative ==="
42i8 is u8
42i8 is i16
2.5f32 is f16
100u64 is i32

// ===== Function with sized param types =====
"=== fn typed params ==="
fn add_i8(x: i8, y: i8) => x + y
add_i8(10i8, 20i8)

fn scale_u16(x: u16, factor: int) => x * factor
scale_u16(100u16, 5)

fn mix_sized(a: i8, b: u32, c: f32) => a + b + c
mix_sized(10i8, 20u32, 1.5f32)

// ===== Function with sized return type =====
"=== fn return types ==="
fn make_i8() i8 { 42i8 }
make_i8()

fn make_u64() u64 { 999u64 }
make_u64()

fn make_f32() f32 { 3.14f32 }
make_f32()

// ===== Function with mixed standard + sized params =====
"=== fn mixed param types ==="
fn combine(x: int, y: i8) => x + y
combine(100, 5i8)

fn to_float(x: u64) => float(x) + 0.5
to_float(100u64)

// ===== Higher-order: pass sized values through generic fn =====
"=== higher-order ==="
fn identity(x) => x
identity(42i8)
type(identity(42i8))
identity(100u64)
type(identity(100u64))
identity(3.14f32)
type(identity(3.14f32))

// ===== Pipe with sized types =====
"=== pipe ==="
42i8 | ~ + 10
100u64 | ~ * 2
3.14f32 | ~ + 1
