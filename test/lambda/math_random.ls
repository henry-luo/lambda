// Test math.random - pure functional PRNG (SplitMix64)

"=== basic usage ==="
let x, seed1 = math.random(42)
x
seed1

"=== chaining seeds ==="
let x2, seed2 = math.random(seed1)
x2
seed2
x != x2

"=== deterministic: same seed same result ==="
let a, sa = math.random(42)
let b, sb = math.random(42)
a == b
sa == sb

"=== different seeds different results ==="
let c, _sc = math.random(0)
let d, _sd = math.random(1)
c != d

"=== seed 0 works ==="
let e, se = math.random(0)
e
se

"=== negative seed works ==="
let f, sf = math.random(-1)
f
sf

"=== generate multiple values ==="
let v1, s1 = math.random(100)
let v2, s2 = math.random(s1)
let v3, s3 = math.random(s2)
let v4, s4 = math.random(s3)
let v5, _s5 = math.random(s4)
v1
v2
v3
v4
v5
