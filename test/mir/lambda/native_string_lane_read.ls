// D3.3.3v3: split's string pointer lane has no value certificate, but its
// physical descriptor suffices for a checked string Item read.
fn read(words: string[] as W, index: int) string? => words[index]

let parts = split("red:blue:green", ":");
[read(parts, 0), read(parts, 2), read(parts, 3), read(split("a:b", ":"), 1)]
