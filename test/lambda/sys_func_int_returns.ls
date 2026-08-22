// Counts return Lambda `int` (the unsized lane), not `i64`.
// Search and ordinal functions return `int | null`; successful values still
// occupy the same native int lane.
// i64() is the one deliberate exception: the explicit widening constructor.
// Before 2026-07-29 len/index_of/last_index_of were declared i64, which made
// ordinary length arithmetic widen into the decimal-backed `integer` carrier.

"=== count / index return types ==="
type(len("abcd"))
type(len([1, 2, 3]))
type(index_of("hello", "l"))
type(last_index_of("hello", "l"))
type(ord("A"))

"=== the widening constructor keeps i64 ==="
type(i64(16))
i64(1.0e17)
type(i64(1.0e17))

"=== length arithmetic stays in the unsized lane ==="
9 - len("abcd")
type(9 - len("abcd"))
len("abcd") + 1
type(len("abcd") + 1)
type(len("abcd") * 2)

"=== values ==="
len("abcd")
index_of("hello", "l")
last_index_of("hello", "l")
index_of("hello", "zz")

"=== nullable absence ==="
type(index_of("hello", "zz"))
type(last_index_of("hello", "zz"))
type(ord(""))
index_of("hello", "zz") or 99
ord("") or 0

"=== typed nullable native result ==="
let empty_text: string = ""
type(ord(empty_text))
ord(empty_text) or 0
