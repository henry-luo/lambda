'=== builtin fn identity ==='
len == len
len != name
name(len)

'=== shortest floats ==='
'sum'
format(0.1 + 0.2)
'exact'
format(0.3)

'=== decimal float canonical ==='
0.1000000000000000055511151231257827m == 0.1
0.1m + 0.2 == 0.3m
0.1 - 0.1m == 0n

'=== depth limit raises ==='
fn deep(n) => if (n == 0) [0] else [deep(n - 1)]
let same^err = deep(260) == deep(260)
^err

'=== vmap numeric hash ==='
1 == 1n
1.0 == 1n
1 == 1.0
let m = map([1, "int", 1.0, "float"])
len(m)
'int key'
m[1]
'float key'
m[1.0]
'decimal key'
m[1n]

let d = map([0.1m, "dec"])
'decimal map float key'
d[0.1]
