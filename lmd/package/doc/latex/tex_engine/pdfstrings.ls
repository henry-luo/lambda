// pdfTeX escapes the UTF-8 bytes of its expanded token text.
pub fn hex_byte(n: int) string => chr(if (n div 16 < 10) 48 + n div 16 else 55 + n div 16) ++
    chr(if (n % 16 < 10) 48 + n % 16 else 55 + n % 16)
fn octal(n: int) string => string(n div 64) ++ string((n div 8) % 8) ++ string(n % 8)

pub fn escape_string(text: string | binary) string^ => join([for (b in binary(text)^)
    if (b < 33 or b > 126) "\\" ++ octal(b)
    else if (b == 40 or b == 41 or b == 92) "\\" ++ chr(b)
    else chr(b)], "")

pub fn escape_name(text: string | binary) string^ => join([for (b in binary(text)^)
    if (b < 33 or b > 126 or contains("#%()<>[]{}/", chr(b))) "#" ++ hex_byte(b)
    else chr(b)], "")

pub fn escape_hex(text: string | binary) string^ => join([for (b in binary(text)^) hex_byte(b)], "")

fn from_nibbles(values: int[], position: int = 0, output: string[] = []) string^ {
    if (position >= len(values)) join(output, "")
    else from_nibbles(values, position + 2, [*output, chr(values[position] * 16 +
        if (position + 1 < len(values)) values[position + 1] else 0)])
}
pub fn unescape_hex(text: string) string^ {
    let digits = [for (c in (split(upper(text), "") or []) where (index_of("0123456789ABCDEF", c) ^ { null }) != null)
        index_of("0123456789ABCDEF", c)];
    from_nibbles(digits)
}
