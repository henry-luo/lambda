// Slice offset rulings: a negative offset is out of range exactly like an
// over-length one, so it clamps instead of wrapping from the end; a null offset
// makes the whole selection absent. Formal semantics §7.1 / §7.4.

"=== array: negative offsets clamp, they do not wrap ==="
[
    slice([1, 2, 3, 4], -2),
    slice([1, 2, 3, 4], -2, 2),
    slice([1, 2, 3, 4], -5, -2),
    slice([1, 2, 3, 4], 1, 99)
]

"=== string: negative offsets clamp, they do not wrap ==="
[
    slice("hello", -2),
    slice("hello", -2, 3),
    slice("hello", -5, -1),
    slice("héllo", -1, 3)
]

"=== null offset is absence, not position 0 ==="
[
    slice("hello", null, 3),
    slice([1, 2, 3, 4], null, 2),
    slice([1, 2, 3, 4], 1, null),
    slice("hello", null)
]

"=== a missed search degrades to null instead of slicing from the start ==="
[
    slice("hello", index_of("hello", "z"), 3),
    slice("hello", index_of("hello", "l"), 4)
]

"=== indexing: negative is out of range, like reading past the end ==="
[[1, 2, 3][-1], [1, 2, 3][3]]
