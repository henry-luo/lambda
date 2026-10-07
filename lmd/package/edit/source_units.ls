// source_units.ls — column units for the source surface
// (vibe/radiant/Radiant_Design_Source_Editor.md CED21).
//
// The model counts code points (S2.5.8), the selection bridge UTF-8 bytes;
// the window parse already reports code points. Both conversions live here,
// and each costs one native UTF-8 encoding of the line, never a per-character
// sum.

// The UTF-8 length of the first `col` code points of `text`.
pub fn bytes_before(text, col) int => if (col <= 0) 0 else len(binary(slice(text, 0, col)))

// A lead byte starts a character; continuation bytes are 0x80–0xBF.
fn is_lead(x) => x < 128 or x >= 192

// The code-point column of byte `offset`: an offset inside a character's
// encoding resolves to that character, one at or past the end to len(text).
pub fn col_at_byte(text, offset) int {
  let bytes = binary(text)
  if (offset <= 0) 0
  else if (offset >= len(bytes)) len(text)
  else len([for (x in slice(bytes, 0, offset + 1) where is_lead(x)) x]) - 1
}
