// The two element axes, pinned together.
//
// `len(e)` counts attributes AND content (S8.3.1v2) and equals what
// `for (m in e)` walks. An IntKey subscript reaches content ONLY (S8.2.1v3).
// `content(e)` is the child sequence, so the child count is `len(content(e))`.
//
// Why both halves live in one fixture: while element `len` wrongly returned the
// child count, the two agreed by accident, and every consumer that pairs a
// count with an index read was written against `len`. Correcting the law made
// each of them walk one position past the last child and read `null` there
// (LR09-8, LR09-9). `last`, the set operators and the mapping pipe are pinned
// below because each was broken exactly that way with no test to catch it.

let e = <div id: "a", cls: "b", <p "x"> <q "y">>

"-- len is attributes + content, and equals the iteration count --"
len(e)
let walked = [for (m in e) m]
walked
len(walked)

"-- content() is the child sequence alone --"
len(content(e))
let kids = [for (c in content(e)) c]
kids

"-- an IntKey reaches children only; past the last child reads null --"
let idx = [e[0], e[1], e[2]]
idx

"-- `last` is the final CHILD, not the final member --"
e[last]

"-- the mapping pipe traverses content, so no phantom row is produced --"
let piped = e |> ~
piped
len(piped)

"-- set operators traverse content too --"
let f = <div z: 9, <p "x">>
let u = e | f
u
let both = e & f
both
let only_e = e ! f
only_e

"-- degenerate shapes --"
len(<bare>); len(content(<bare>))
len(<attrs_only a: 1, b: 2>); len(content(<attrs_only a: 1, b: 2>))
let content_only = <kids_only <a> <b> <c>>
len(content_only); len(content(content_only))

"-- a NOMINAL element obeys the same law --"
type Box { label: string, string* }
let b = <Box label: "t", "c">
len(b); len(content(b))

"-- a group element: attributes are the KEY, members are the content --"
let rows = [{r: "w", n: 1}, {r: "e", n: 2}, {r: "w", n: 3}]
let groups = [for (x in rows group by x.r into g) [len(g), len(content(g)), sum(g |> ~["n"])]]
groups
