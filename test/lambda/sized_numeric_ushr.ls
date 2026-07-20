// Logical right shifts retain the unsigned width of their left operand.
let u8_result = ushr(-1i8, 1)
let u32_result = ushr(-1i32, 1)
let u64_result = ushr(-1i64, 1)
let max_u64_result = ushr(18446744073709551615u64, 1)
let plain_result = ushr(-1, 1)
let exhausted = ushr(255u8, 8)

[[u8_result, type(u8_result)],
 [u32_result, type(u32_result)],
 [u64_result, type(u64_result)],
 [max_u64_result, type(max_u64_result)],
 [plain_result, type(plain_result)],
 [exhausted, type(exhausted)]]
