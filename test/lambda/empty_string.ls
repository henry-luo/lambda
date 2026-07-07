// Phase 3: empty string is a real string value, and is falsy.

'=== literal empty string ==='
let empty = ""
empty == null
empty != null
empty == ""
empty is string
type(empty)
len(empty)
not empty
empty or "fallback"
empty and "rhs"

'=== non-empty contrast ==='
let text = "x"
text == null
text is string
not text
text or "fallback"
text and "rhs"

'=== match and filters ==='
fn classify(v) => match v {
    case string: "string"
    default: "other"
}
classify(empty)
classify(null)

let data = [
    {name: "Alice", value: "hello"},
    {name: "Bob", value: ""},
    {name: "Charlie", value: "world"}
]
let non_empty = data that (~.value != "")
for (x in non_empty) x.name
let only_empty = data that (~.value == "")
for (x in only_empty) x.name

'=== data-derived empty string ==='
let parsed^err = parse("{\"name\":\"\"}", 'json')
parsed.name == ""
parsed.name == null
parsed.name is string
len(parsed.name)

'=== string pattern ==='
type zero_or_more_a = "a"*
empty is zero_or_more_a
"aaa" is zero_or_more_a
