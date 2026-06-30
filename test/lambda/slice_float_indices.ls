// Integral float indices should work for slice and string indexing.

"=== string slice integral floats ==="
let s = "abcdefghi"
let char_idx = 0 / 8
[
    string(char_idx),
    slice(s, char_idx, char_idx + 1),
    ord(slice(s, char_idx, char_idx + 1)),
    slice(s, 7.0, 8.0),
    ord(s[7.0])
]

"=== UTF-8 string slice integral floats ==="
[
    slice("café", 3.0, 4.0),
    ord(slice("café", 3.0, 4.0)),
    ord("café"[3.0])
]

"=== array slice integral floats ==="
[
    slice([10, 20, 30, 40], 1.0, 3.0),
    slice([10, 20, 30, 40], 2.0),
    slice([10, 20, 30, 40], -2.0, -0.0)
]

"=== fractional float indices reject ==="
[
    slice(s, 1.5, 2.5),
    slice([10, 20, 30], 1.5, 2.5)
]
