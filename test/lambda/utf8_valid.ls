import utf8: lambda.io.utf8
let valid = [binary("")^, b'\x00', b'\x7F', b'\xC280', b'\xDFBF', b'\xE0A080', b'\xF0908080', b'\xF48FBFBF', binary("café 😀")^]
let invalid = [b'\x80', b'\xC0AF', b'\xC2', b'\xE08080', b'\xEDA080', b'\xF0808080', b'\xF4908080', b'\xF5808080', b'\xE2A041'];
[
  all([for (s in valid) utf8.is_valid(s)^]),
  all([for (s in invalid) not utf8.is_valid(s)^]),
  all([for (s in valid) (utf8.decode(s) ^ { null }) != null]),
  all([for (s in invalid) (utf8.decode(s) ^ { null }) == null])
]
