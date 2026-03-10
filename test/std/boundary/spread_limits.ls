// Test: Spread Limits
// Layer: 3 | Category: boundary | Covers: large spread, nested, empty collections

// ===== Spread empty array =====
let a = [*[]]
a
len(a)

// ===== Spread empty list =====
let b = (*())
b

// ===== Spread single element =====
[*[42]]

// ===== Spread into array =====
let xs = [1, 2, 3]
let ys = [4, 5, 6]
[*xs, *ys]

// ===== Spread into list =====
let la = (1, 2, 3)
let lb = (4, 5, 6)
(*la, *lb)

// ===== Spread into map =====
let m1 = {a: 1, b: 2}
let m2 = {c: 3, d: 4}
{*:m1, *:m2}

// ===== Spread map override =====
let base = {x: 1, y: 2, z: 3}
let override = {y: 20, z: 30}
{*:base, *:override}

// ===== Nested spread =====
let inner = [1, 2]
let middle = [*inner, 3, 4]
let outer = [*middle, 5, 6]
outer

// ===== Spread in for =====
for (x in [*[1, 2], *[3, 4], *[5, 6]]) x * 10

// ===== Large spread =====
let big1 = for (i in 1 to 50) i
let big2 = for (i in 51 to 100) i
let combined = [*big1, *big2]
len(combined)
combined[0]
combined[99]

// ===== Spread preserving types =====
let ints = [1, 2, 3]
let strs = ["a", "b", "c"]
let mixed = [*ints, *strs]
len(mixed)
mixed[0]
mixed[3]

// ===== Spread map into map with many fields =====
let config_base = {host: "localhost", port: 8080, debug: false, log: true, cache: true}
let config_prod = {*:config_base, host: "prod.com", debug: false, port: 443}
config_prod.host
config_prod.port
config_prod.log
