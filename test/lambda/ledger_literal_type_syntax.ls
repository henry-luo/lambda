// S11.1.1v3/S7.4.2 (LR02-30/31): type slots use the same literal decoders and error unions.
type D = t'2025-01-01'
type B = b'\xDEAD'
type M = 1.5m
type U = 5u8
type I = 9n
type Neg = -3
type Min = -128i8
fn echo(x: int^) int | error => x
let value: int^ = error("kept");
[t'2025-01-01' is D, b'\xDEAD' is B, 1.5m is M, 5u8 is U,
 9n is I, -3 is Neg, -128i8 is Min, value is error, echo(3), echo(error("x")) is error]
