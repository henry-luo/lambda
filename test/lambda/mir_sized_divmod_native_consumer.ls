// S4.5.3, D2.4.1: sized div/mod leave the machine domain before native consumers.
fn remainder_next(value: u8, divisor: int) => value % divisor + 1
fn quotient_next(value: u8, divisor: int) => value div divisor + 1
fn signed_next(value: i8, divisor: u8) => [value % divisor + 1, value div divisor + 1]
fn low_nibbles(bytes: binary) array^ => [for (byte in bytes)
    slice("0123456789abcdef", byte % 16, byte % 16 + 1)^]
let results = [for (value in [0u8, 1u8, 15u8, 16u8, 173u8, 255u8])
    [remainder_next(value, 16), quotient_next(value, 16)]];
[results, low_nibbles(b'\x00ADFF')^, signed_next(-127i8, 16u8),
 remainder_next(1u8, 0), quotient_next(1u8, 0)]
