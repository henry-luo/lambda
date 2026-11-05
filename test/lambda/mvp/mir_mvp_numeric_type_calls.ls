// D4.2.1/D2.3.1: explicit type calls convert values; they are not Function calls.
fn narrow(value: any) any => u8(value)
fn signed(value: any) any => i16(value)
fn single(value: any) any => f32(value)
fn wide(value: any) any => u64(value)
let byte_value = narrow(260)
let signed_value = signed(65535)
let single_value = single(1.25)
let wide_value = wide(4294967297)
let rejected = wide(error("retained"))
let retained = [byte_value, signed_value, single_value, wide_value]
let churn = for (i in 1 to 90) [float(i) + 0.25];
[byte_value == 4, byte_value is u8, signed_value == -1, signed_value is i16,
 single_value == 1.25, single_value is f32, wide_value == 4294967297,
 wide_value is u64, rejected is error, retained[0] == 4, retained[3] == 4294967297]
