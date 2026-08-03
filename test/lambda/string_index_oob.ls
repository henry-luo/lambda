// out-of-range string indexing returns a real empty string.

let ascii = "abc"
let unicode = "é"

[
    ascii[3] == "",
    ascii[-1] == "",
    unicode[1] == "",
    type(ascii[3])
]
