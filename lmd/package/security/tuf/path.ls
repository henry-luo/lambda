// D11.1.2, D11.2.4: opaque role labels and relative URI references are data,
// never native filesystem paths. Encode components before host URL parsing.
import utf8: lambda.io.utf8
import meta: lambda.security.tuf.metadata

fn unreserved(byte: int) bool => (byte >= 65 and byte <= 90) or (byte >= 97 and byte <= 122) or
    (byte >= 48 and byte <= 57) or contains([45, 46, 95, 126], byte)
fn printable(text, ceiling: int) bool {
    if (not (text is string)) { return false }
    // conversion failures must remain a rejected name in this total predicate.
    let raw = binary(text) ^ { return false }
    let checked = utf8.decode(raw) ^ { return false }
    let chars = split(checked, "") ^ { return false };
    len(raw) > 0 and len(raw) < ceiling and
        (all([for (c in chars) (let cp = ord(c), cp >= 32 and not (cp >= 127 and cp <= 159))]) ^ { false })
}
pub fn role_name(text) bool => printable(text, 129) ^ { false }
fn segments(text: string) bool^ => all([for (part in split(text, "/")) part != "" and part != "." and part != ".."])
fn decoded_path(text: string, iri: bool) array?^ {
    let raw = binary(text)^
    var cursor = 0, decoded = []
    while (cursor < len(raw)) {
        let byte = raw[cursor];
        if (byte == 37) {
            if (cursor + 2 >= len(raw)) { return null }
            let high = index_of("0123456789ABCDEF", chr(raw[cursor + 1]))^
            let low = index_of("0123456789ABCDEF", chr(raw[cursor + 2]))^;
            if (high == null or low == null) { return null }
            let value = high * 16 + low;
            if (unreserved(value)^) { return null }
            decoded = [*decoded, value]
            cursor = cursor + 3
        } else {
            if (not unreserved(byte)^ and byte != 47 and
                (not iri or byte < 32 or byte == 127 or contains([92, 64, 63, 35], byte)^)) { return null }
            decoded = [*decoded, byte]
            cursor = cursor + 1
        }
    }
    decoded
}
fn valid_path(text, iri: bool) bool^ {
    if (not (printable(text, 4096) ^ { false }) or not segments(text)^) { return false }
    let decoded = decoded_path(text, iri)^;
    if (decoded == null) false else printable(utf8.decode(binary(decoded)) ^ { null }, 4096) ^ { false }
}
pub fn target_name(text) bool => valid_path(text, true) ^ { false }
pub fn repository_path(text) bool => valid_path(text, false) ^ { false }
fn encoded(text: string, preserve: bool) string^ {
    if (not printable(text, 4096)^) raise meta.invalid("invalid URI component")
    let raw = binary(text)^
    var cursor = 0, parts = []
    while (cursor < len(raw)) {
        let byte = raw[cursor];
        if (preserve and byte == 37) {
            parts = [*parts, "%" ++ chr(raw[cursor + 1]) ++ chr(raw[cursor + 2])]
            cursor = cursor + 3
        } else {
            parts = [*parts, if (unreserved(byte)) chr(byte) else "%" ++ upper(meta.hex(binary([byte]))^)]
            cursor = cursor + 1
        }
    }
    return join(parts, "")
}
pub fn component(text) string^ => encoded(text, false)^
pub fn target_path(text) string^ {
    if (not target_name(text)) raise meta.invalid("invalid target reference")
    else join([for (part in split(text, "/")^) encoded(part, true)^], "/")
}
