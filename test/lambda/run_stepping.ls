// S8.2.6: a run is stepped set-at-a-time by every key kind except positional
let e = <div id: "x", <p b: 1> <p b: 2, <p b: 3>>>
type p = <p>

"a name maps over a query run"
e?<p>.b
"a stored run steps as the inline one"
let ps = e?<p>
ps.b
"the dynamic name form"
ps["b"]
"positional keys are a window over the run"
ps[0]
; (e?<p>)[1 to 2]
"the per-item positional reading is a pipe"
e?<p> |> ~[0]
"a missing name drops, the run collapses"
ps.missing
; (<a x: 1>, <a>).x
"type key and wildcard per item"
ps[p]
ps.*
"root and parent with no traversal context"
ps./
ps.~~
"a run of paths selects each parent"
; (\.a.b, \.c.d).~~
; (\.a.b, \.c.d)./
"an array is not a run"
let arr = [{b: 1}, {b: 2}]
arr.b
arr.*.b
"the list's own members first"
ps.length
"a list-valued field stays one item"
; ({b: (1, 2)}, {b: 3}).b

