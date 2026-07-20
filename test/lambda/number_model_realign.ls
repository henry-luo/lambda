// Type-directed numeric promotion, sized-lane arithmetic, and storage-lifetime coverage.
fn show(x) => [type(x), x]

"=== sized lanes ==="
show(127i8 + 1i8)
show(32767i16 + 1i16)
show(2147483647i32 + 1i32)
show(9223372036854775807i64 + 1i64)
show(255u8 + 1u8)
show(65535u16 + 1u16)
show(4294967295u32 + 1u32)
show(18446744073709551615u64 + 1u64)
show(1i8 + 1u8)
show(1i32 + 1u32)
show(1i64 + 1u64)
show(-128i8 div -1i8)

"=== bitwise lanes ==="
show(band(1i8, 255u8))
show(bor(1i64, 2u64))
show(bxor(1u8, 3))
show(shl(1i8, 8u8))
show(shl(1i64, 1))
show(shr(-8i8, 2u8))
show(bnot(1n))

"=== semantic entry ==="
show(255u8 + 1)
show(1 + 255u8)
show(1i64 + 1)
show(1 + 1i64)
show(9223372036854775807u64 + 1)
show(9223372036854775808u64 + 1)
show(18446744073709551615u64 + 1)
show(1i64 + 0.5)
show(0.5 + 1i64)
show(1u64 + 0.5)
show(0.5 + 1u64)

"=== division ==="
show(7i8 / 2u8)
show(7i64 / 2u64)
show(7i64 div 2u64)
show(7i64 % 2u64)
show(7i64 div 2)
show(7i64 % 2)
show(7n / 2n)
show(7n div 2n)
show(7n % 2n)
show(3 / 2)
show(1.0 / 0.0)
show(-1.0 / 0.0)

"=== int overflow ==="
show(9007199254740991 + 1)
show(9007199254740991 + 2)
show(9007199254740991 * 3)

"=== vectors and folds ==="
let lane = [255u8, 1u8] + [1u8, 2u8]
[type(lane[0]), lane]
let wide = [7i64, 9i64] / [2u64, 2u64]
[type(wide[0]), wide]
let quotient = [7i64, 9i64] div [2u64, 2u64]
[type(quotient[0]), quotient]
show(sum([127i8, 1i8]))
show(sum([9223372036854775807i64, 1i64]))
show(sum([9007199254740991, 2]))
show(math.prod([64i8, 2i8]))
show(math.mean([1i8, 2i8]))
show(math.mean([1i64, 2i64]))
let running = math.cumsum([127i8, 1i8, 1i8])
[type(running[0]), running]

"=== exact comparisons and selection ==="
18446744073709551615u64 gt 9223372036854775807i64
18446744073709551615u64 lt 18446744073709551616.0
18446744073709551615u64 == 18446744073709551616.0
show(min(18446744073709551615u64, 9223372036854775807i64))
show(max(18446744073709551615u64, 9223372036854775807i64))
argmin([18446744073709551615u64, 18446744073709551614u64])
argmax([18446744073709551614u64, 18446744073709551615u64])
show(math.dot([18446744073709551615u64], [1u64]))

"=== homes and persistence ==="
fn add_one(x: u64) => x + 1
show(add_one(1u64))
show(add_one(18446744073709551615u64))
fn make_reader(x: u64) {
    let saved = x + 1
    fn read() => saved
    read
}
let read_small = make_reader(1u64)
let read_wide = make_reader(18446744073709551615u64)
show(read_small())
show(read_wide())
let stored = [1u64 + 1, 18446744073709551615u64 + 1]
[[type(stored[0]), stored[0]], [type(stored[1]), stored[1]]]
let record = {small: 1i64 + 1, wide: 18446744073709551615u64 + 1}
[[type(record.small), record.small], [type(record.wide), record.wide]]
show(add_one(1u64))
show(add_one(18446744073709551615u64))
