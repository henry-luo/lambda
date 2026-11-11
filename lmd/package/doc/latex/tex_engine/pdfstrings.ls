// pdfTeX escapes the UTF-8 bytes of its expanded token text.
fn hex(n) any^ => slice("0123456789ABCDEF", n div 16, n div 16 + 1) ++
    slice("0123456789ABCDEF", n % 16, n % 16 + 1)
fn octal(n) => string(n div 64) ++ string((n div 8) % 8) ++ string(n % 8)

pub fn escape_string(text) any^ => join([for (b in binary(text)^)
    if (b < 33 or b > 126) "\\" ++ octal(b)
    else if (b == 40 or b == 41 or b == 92) "\\" ++ chr(b)
    else chr(b)], "")

pub fn escape_name(text) any^ => join([for (b in binary(text)^)
    if (b < 33 or b > 126 or contains("#%()<>[]{}/", chr(b))) "#" ++ hex(b)
    else chr(b)], "")

pub fn escape_hex(text) any^ => join([for (b in binary(text)^) hex(b)], "")

fn from_nibbles(values, position = 0, output = []) any^ {
    if (position >= len(values)) join(output, "")
    else from_nibbles(values, position + 2, [*output, chr(values[position] * 16 +
        if (position + 1 < len(values)) values[position + 1] else 0)])
}
pub fn unescape_hex(text) any^ {
    let digits = [for (c in (split(upper(text), "") ^ { [] }) where (index_of("0123456789ABCDEF", c) ^ { null }) != null)
        index_of("0123456789ABCDEF", c)];
    from_nibbles(digits)
}
