// Test explicit sized numeric annotations and Go-like fixed-width numeric behavior.

"=== scalar annotations ==="
let a_source = 49734321
let a: i32 = i32(a_source);
[a, type(a)]
let b_source = 128
let b: i8 = i8(b_source);
[b, type(b)]
let c_source = 256
let c: u8 = u8(c_source);
[c, type(c)]
let d16_source = 32768
let d16: i16 = i16(d16_source);
[d16, type(d16)]
let e16_source = 65536
let e16: u16 = u16(e16_source);
[e16, type(e16)]
// run-time operand: `-1u32` alone is a constant overflow (S4.1.5)
let one_u32 = 1u32
let d: u32 = 0u32 - one_u32;
[d, type(d)]
let e: u64 = 18446744073709551615u64;
[e, type(e)]

"=== params ==="
fn take_i32(x: i32) => [x, type(x)]
let param_i32_source = 2147483648
let param_i32: i32 = i32(param_i32_source)
take_i32(param_i32)
fn take_u8(x: u8) => [x, type(x)]
let param_u8_source = 256
let param_u8: u8 = u8(param_u8_source)
take_u8(param_u8)
fn take_u16(x: u16) => [x, type(x)]
let param_u16_source = 65536
let param_u16: u16 = u16(param_u16_source)
take_u16(param_u16)

"=== returns ==="
let ret_i8_source = 128
fn ret_i8() i8 => i8(ret_i8_source);
[ret_i8(), type(ret_i8())]
fn ret_u32() u32 => 0u32 - one_u32;
[ret_u32(), type(ret_u32())]

"=== fixed-width arithmetic ==="
// run-time operands: a literal-only overflow is a compile error (S4.1.5)
let seed_i32 = 49734321i32
let max_i8 = 127i8
let max_u8 = 255u8
let max_i32 = 2147483647i32
let max_u32 = 4294967295u32
seed_i32 * 1103515245i32
type(seed_i32 * 1103515245i32)
max_i8 + 1i8
type(max_i8 + 1i8)
max_u8 + 1u8
type(max_u8 + 1u8)
max_i32 + 1i32
type(max_i32 + 1i32)
max_u32 + 1u32
type(max_u32 + 1u32)

"=== mixed promotion ==="
42i8 + 10u8
type(42i8 + 10u8)
65535u16 + 1i16
type(65535u16 + 1i16)
2147483647i32 + 1u32
type(2147483647i32 + 1u32);
-1i32 + 4294967295u32
type(-1i32 + 4294967295u32)
let max_u64 = 18446744073709551615u64
max_u64 + 1u64
type(max_u64 + 1u64)

"=== integer operators ==="
5i8 div 2i8
type(5i8 div 2i8)
5i8 % 2i8
type(5i8 % 2i8)
255u8 div 2u8
type(255u8 div 2u8)
255u8 % 2u8
type(255u8 % 2u8)

"=== unary wrap ===";
// run-time operands: negating a constant out of its lane is a compile error (S4.1.5)
let neg_i8 = -128i8
let pos_u8 = 1u8
let wrap_i64 = 9223372036854775807i64;
-neg_i8
type(-neg_i8);
-pos_u8
type(-pos_u8);
-(wrap_i64 + 1i64)
type(-(wrap_i64 + 1i64))

"=== bitwise ==="
band(4294967295u32, 255u32)
bor(256u32, 1u8)
bnot(0u32)
shr(-1i32, 1u8)

"=== typed array store ==="
let arr_second = 256
let arr_one = 1u8
let arr: u8[] = [255u8, u8(arr_second), 0u8 - arr_one]
arr
type(arr[0])
type(arr[1])
type(arr[2])
