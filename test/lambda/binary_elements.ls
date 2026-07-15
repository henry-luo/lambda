// Binary element operations use byte semantics while mixed text concatenation stays textual.
let bytes = b'\x00ADFF'
let joined = b'\xDEAD' ++ b'\xBEEF'
let nul_joined = b'\x00FF' ++ b'\x00AD'
let iterated = [for (byte in bytes) byte]
[
  bytes[0],
  bytes[0] is u8,
  bytes[1],
  bytes[1] is u8,
  bytes[2],
  bytes[2] is u8,
  bytes[-1],
  bytes[3],
  bytes[0 to 0],
  bytes[1 to 2],
  bytes[1 to 2] is binary,
  bytes[0 to 99],
  0 in bytes,
  173 in bytes,
  173u8 in bytes,
  173.0 in bytes,
  256 in bytes,
  "173" in bytes,
  joined,
  joined is binary,
  b'\x00' ++ b'\xAD' ++ b'\xFF' == bytes,
  len(joined),
  nul_joined,
  len(nul_joined),
  b'\xDEAD' ++ "tail",
  "head" ++ b'\xBEEF',
  iterated,
  [for (byte in bytes) byte is u8],
  [for (i, byte in bytes) [i, byte]]
]
