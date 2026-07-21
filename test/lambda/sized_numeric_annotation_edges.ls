// Test explicit sized numeric annotations and Go-like fixed-width numeric behavior.

"=== scalar annotations ==="
let a: i32 = 49734321
[a, type(a)]
let b: i8 = 128
[b, type(b)]
let c: u8 = 256
[c, type(c)]
let d16: i16 = 32768
[d16, type(d16)]
let e16: u16 = 65536
[e16, type(e16)]
let d: u32 = -1
[d, type(d)]
let e: u64 = 18446744073709551615u64
[e, type(e)]

"=== params ==="
fn take_i32(x: i32) => [x, type(x)]
let param_i32: i32 = 2147483648
take_i32(param_i32)
fn take_u8(x: u8) => [x, type(x)]
let param_u8: u8 = 256
take_u8(param_u8)
fn take_u16(x: u16) => [x, type(x)]
let param_u16: u16 = 65536
take_u16(param_u16)

"=== returns ==="
fn ret_i8() i8 => 128
[ret_i8(), type(ret_i8())]
fn ret_u32() u32 => -1
[ret_u32(), type(ret_u32())]

"=== fixed-width arithmetic ==="
49734321i32 * 1103515245i32
type(49734321i32 * 1103515245i32)
127i8 + 1i8
type(127i8 + 1i8)
255u8 + 1u8
type(255u8 + 1u8)
2147483647i32 + 1i32
type(2147483647i32 + 1i32)
4294967295u32 + 1u32
type(4294967295u32 + 1u32)

"=== mixed promotion ==="
42i8 + 10u8
type(42i8 + 10u8)
65535u16 + 1i16
type(65535u16 + 1i16)
2147483647i32 + 1u32
type(2147483647i32 + 1u32)
-1i32 + 4294967295u32
type(-1i32 + 4294967295u32)
18446744073709551615u64 + 1u64
type(18446744073709551615u64 + 1u64)

"=== integer operators ==="
5i8 div 2i8
type(5i8 div 2i8)
5i8 % 2i8
type(5i8 % 2i8)
255u8 div 2u8
type(255u8 div 2u8)
255u8 % 2u8
type(255u8 % 2u8)

"=== unary wrap ==="
-(-128i8)
type(-(-128i8))
-(1u8)
type(-(1u8))
-(9223372036854775807i64 + 1i64)
type(-(9223372036854775807i64 + 1i64))

"=== bitwise ==="
band(4294967295u32, 255u32)
bor(256u32, 1u8)
bnot(0u32)
shr(-1i32, 1u8)

"=== typed array store ==="
let arr: u8[] = [255, 256, -1]
arr
type(arr[0])
type(arr[1])
type(arr[2])
