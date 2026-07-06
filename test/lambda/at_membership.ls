'=== at membership ==='
let xs = [10, 20, 30]
0 at xs
2 at xs
3 at xs
-1 at xs
20 in xs
20 at xs

let m = {a: 1, b: null}
'a' at m
'b' at m
'c' at m
1 in m
null in m

let el = <item id:"i1", empty:null; "child">
'id' at el
'empty' at el
'missing' at el
"child" in el
'id' in el

'=== for at ==='
[for (k at el) k]

