// S8.2.5: `.*` is the item step (≡ `e[any]`) and `.**` its closure (≡ `e.?any`)
let e = <div id: "x", <p b: 1> <p b: 2, <p b: 3>>>
type p = <p>

"items of an element: attribute values, then children"
e.*
"a name after the item step"
e.*.b
"the closure includes self, in document order"
e.**.b
"two item steps splice"
e.*.*
"grandchildren that are p"
e.*[p]
"closure then type key"
e.**[p]

"named and indexed members are both items"
let users = [{name: "ann", age: 31}, {name: "bob", age: 42}]
users.*.name
users.*.age
let m = {a: {b: 1}, c: {b: 2}, d: 5}
m.*
m.*.b
m.**

"a quoted '*' is an ordinary name"
; {'*': 7}.'*'
m.'*'

"text is a scalar to the walk"
"abc".*
"abc".**

"absent and degenerate bases"
null.*
; [].*
; [5].*
; (1 to 3).*
count(e.*)
count(e.**)

