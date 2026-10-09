// S17.6.1 (SP21): `replace` steps through matches as ECMAScript replaceAll
// does, empty matches included, and inserts the replacement literally, with or
// without options. The plain call had used RE2's GlobalReplace: it skipped an
// empty match after a non-empty one and rewrote `\0`..`\9`. An empty options
// map sets no option (it had returned an error).

'1. empty matches'
"1.1"; [replace("aab", \("a"*), "-")]
"1.2"; [replace("aab", \("a"*), "-", {limit: 9})]
"1.3"; [replace("ab", \("\d"*), "-")]
"1.4"; [replace("", \("\d"*), "-")]
"1.5"; (find("aab", \("a"*)) |> ~.index)

'2. the replacement is literal text'
"2.1"; [replace("a1b", \("\d"), "<\\0>")]
"2.2"; [replace("a1b", \("\d"), "$&")]
"2.3"; [replace("a1b2", \("\d"), "\\1", {limit: 1})]

'3. options select among the same matches'
"3.1"; [replace("a1b2c3", \("\d"), "#", {limit: 2})]
"3.2"; [replace("a1b2c3", \("\d"), "#", {last: 1})]
"3.3"; [replace("aAa", \("a"), "-", {ignore_case: true})]

'4. an empty options map'
"4.1"; [replace("aab", "a", "-", {})]
"4.2"; (find("aab", "a", {}) |> ~.index)
"4.3"; [replace("aab", \("a"), "-", {})]
