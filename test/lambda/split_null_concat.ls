// Test split() with various delimiters and null concatenation
// Validates that split correctly separates strings and null ++ string works

"===== split tests ====="

"split on comma:"
split("a,b,c", ",")

"split on space:"
split("hello world foo", " ")

"split on double colon:"
split("a::b::c", "::")

"split no match:"
split("hello", "x")

"split on dash:"
split("one-two-three", "-")

"===== split space - verify element count ====="

fn count_parts(s, delim) {
    len(split(s, delim))
}

"parts in 'a b c':"
count_parts("a b c", " ")

"parts in 'hello':"
count_parts("hello", " ")

"parts in 'one two three four':"
count_parts("one two three four", " ")

"===== split and rejoin roundtrip ====="

fn split_rejoin(s, delim) {
    str_join(split(s, delim), delim)
}

"roundtrip comma:"
split_rejoin("a,b,c", ",")

"roundtrip space:"
split_rejoin("hello world", " ")

"roundtrip double colon:"
split_rejoin("x::y::z", "::")

"===== null concatenation tests ====="

"null ++ string:"
null ++ " world"

"string ++ null:"
"hello " ++ null

"===== null in fn concat ====="

fn safe_concat(a, b) {
    a ++ b
}

"safe_concat(null, text):"
safe_concat(null, "text")

"safe_concat(text, null):"
safe_concat("text", null)

"safe_concat(hello, world):"
safe_concat("hello", " world")

"===== ALL TESTS DONE ====="
