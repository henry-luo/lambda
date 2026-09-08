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
let same = deep(260) == deep(260) ^ { ^ }
same is error

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

let d = map([1.0m, "dec"])
'decimal map float key'
d[1.0]

'=== vmap canonical keys ==='
1.0m == 1.00m
let decimal_order = (1.0m < 1.00m)
decimal_order
let scaled = map([1, "int", 1.0, "float", 1.0m, "decimal", 1.00m, "scaled", 1n, "integer"]);
[len(scaled), scaled[1], scaled[1.0], scaled[1.00m], scaled[1n]]
let named = map(["name", "string", 'name', "symbol"]);
[len(named), named["name"], named['name']]
let fractional_key = map([1.5, "fractional"]) ^ { ^ }
fractional_key is error
let decimal_key = map([1.5m, "decimal"]) ^ { ^ }
decimal_key is error
let negative_key = map([-1, "negative"])
negative_key[-1]
