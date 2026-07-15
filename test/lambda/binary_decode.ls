// Binary literals are immutable decoded bytes regardless of authoring encoding.
let hex = b'A0 fe'
let marked_hex = b'\x A0 FE'
let base64 = b'\64oP4='
let base64_unpadded = b'\64oP4'
let nul_bytes = b'\x00FF00'
fn byte_len(value: binary) { len(value) }

[
  hex,
  marked_hex,
  base64,
  hex == marked_hex,
  marked_hex == base64,
  base64 == base64_unpadded,
  nul_bytes,
  len(nul_bytes),
  byte_len(nul_bytes),
  string(nul_bytes),
  "prefix: " ++ nul_bytes,
  binary("hello"),
  binary("hello") is binary,
  len(binary("hello")),
  binary(42),
  binary(1.5),
  format(marked_hex, 'mark') == "b'\\xA0FE'",
  format(marked_hex, 'json') == "\"oP4=\""
]
