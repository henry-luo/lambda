// Physical layout metadata is confined to binary declarations.
type Pair : binary & {endian: 'big'} {
    unsigned_value: u16,
    signed_value: i16
}
type Nested : binary {
    pair: Pair,
    tail: u8 & {offset: 5}
}
type Wide : binary & {endian: 'big'} {
    signed_value: i64,
    unsigned_value: u64,
    fraction: f64
}
type Inherited : binary {value: u16}
type Ordered : binary & {endian: 'big'} {nested: Inherited}
type Padded : binary & {endian: 'little'} {
    first: u8
        & {offset: 1},
    second: u16 & {offset: 4}
}
type Plain : binary & {} {value: u8 & {}}
// Outside binary slots, the same map spelling remains an intersection pattern.
type Logical = {value: u32} & {offset: 4};
type Positive = int that (~ > 0);
let pair = binary_decode(b'\x1234FFFE', Pair)^;
let nested = binary_decode(b'\x1234FFFE0007', Nested)^;
let wide = binary_decode(b'\x8000000000000000FFFFFFFFFFFFFFFF3FF4000000000000', Wide)^;
let ordered = binary_decode(b'\x1234', Ordered)^;
let padded = binary_decode(b'\x000700003412', Padded)^;
let plain = binary_decode(b'\x09', Plain)^;
[
    pair.unsigned_value == 4660,
    pair.signed_value == -2,
    pair is Pair,
    pair is element,
    nested.pair.unsigned_value == 4660,
    nested.tail == 7,
    (binary_decode(b'\x12', Pair) ^ {true}) == true,
    wide.signed_value == i64("-9223372036854775808"),
    wide.unsigned_value == 18446744073709551615u64,
    wide.fraction == 1.25,
    ordered.nested.value == 4660,
    padded.first == 7 and padded.second == 4660,
    plain.value == 9,
    {value: 5u32, offset: 4} is Logical,
    not ({value: 5u32} is Logical),
    5 is Positive and not (-1 is Positive)
]
