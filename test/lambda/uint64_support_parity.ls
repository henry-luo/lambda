// uint64 should participate anywhere the runtime accepts int64 numeric values.

"=== scalar math ==="
math.sqrt(4u64)
trunc(4u64)
sign(4u64)
type(binary(42u64))
type(symbol(42u64))
[format(18446744073709551615u64, "json")]

"=== vector math and truth ==="
abs([1u64, 2u64])
round([1u64, 2u64])
floor([1u64, 2u64])
ceil([1u64, 2u64])
-[1u64, 2u64]
all([0u64, 1u64])
any([0u64, 1u64])

"=== typed arrays ==="
let wide: u64[] = [1u64, 18446744073709551615u64]
wide
type(wide[0])
type(wide[1])
let converted: u64[] = [1, 2]
converted
type(converted[0])
let filled = fill(2, 18446744073709551615u64)
filled
type(filled[0])

"=== MIR raw parity ==="
fn add_u64(left: u64, right: u64) u64 => left + right
let raw_local: u64 = add_u64(1u64, 2u64)
raw_local
type(raw_local)
add_u64(18446744073709551615u64, 1u64)
type(add_u64(18446744073709551615u64, 1u64))
wide[1]
type(wide[1])
