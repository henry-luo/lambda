// S2.2.2v2 / S8.2.2v3: the empty name iterates as the predefined symbol.empty.

'=== constant ==='
let e = symbol.empty
e
type(e)
len(e)
string(e) == ""
e == symbol.empty
let t = symbol
t.empty == e
symbol("") == null

'=== json empty key ==='
let obj = parse("{\"a\":1,\"\":2}", 'json') ^ { null }
len(obj) == len([for (k, v in obj) k])
;[for (k, v in obj) k]
;[for (k at obj) k]
;[for (k, v in obj) v]
obj[symbol.empty]
obj[""] == obj[e]

'=== properties empty key ==='
let props = parse("a.b=1\n=root\n", 'properties') ^ { null }
;[for (k, v in props) k]
props[symbol.empty]
