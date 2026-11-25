// The index and comparison operands cross calls so constant folding cannot settle them.
fn byte_at(bytes: binary, pos) => bytes[pos]
fn final_byte(bytes: binary) => bytes[last]
fn compare(value, bound: int) => [value < bound, value <= bound,
    value > bound, value >= bound, value == bound, value != bound]

let bytes = b'\x00ADFF';
[
    byte_at(bytes, 0) is u8,
    byte_at(bytes, 1),
    byte_at(slice(bytes, 1, 3), 1),
    byte_at(bytes, -1),
    byte_at(bytes, 3),
    byte_at(bytes, 1.0),
    byte_at(bytes, 1.0m),
    byte_at(bytes, 1.5),
    byte_at(bytes, 18446744073709551615u64),
    final_byte(bytes),
    compare(byte_at(bytes, 0), -1),
    compare(byte_at(bytes, 1), 173),
    compare(byte_at(bytes, 2), 256),
    compare(byte_at(bytes, 3), 256),
    compare(0.0 / 0.0, 0),
    compare(9007199254740993i64, 9007199254740991)
]
