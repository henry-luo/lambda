// Test: if-else expressions where branches have same semantic type
// but different C return types (e.g., String* vs Item)
// This covers the C2MIR "incompatible types in cond-expression" fix

"1. string() vs str_join() - String* vs Item"
let parts = ["a", "b", "c"]
if (len(parts) == 1) string(parts[0]) else str_join(parts, "|")

"2. string() vs string() - both String*"
if (true) string(1) else string(2)

"3. int literal branches (fast path)"
if (true) 1 else 2

"4. float literal branches (fast path)"
if (true) 1.5 else 2.5

"5. let binding with mixed string types"
let y = if (len(parts) == 1) string(parts[0]) else str_join(parts, "|")
y

"6. string literal vs str_join"
if (false) "single" else str_join(parts, "-")

"7. nested if-else with mixed string types"
let z = if (len(parts) > 2) str_join(parts, ",") else if (len(parts) == 1) string(parts[0]) else "empty"
z

"8. bool branches"
if (true) true else false

"9. string vs format"
if (true) string(42) else format(3.14)

"10. single part takes then branch"
let single = ["only"]
if (len(single) == 1) string(single[0]) else str_join(single, "|")
