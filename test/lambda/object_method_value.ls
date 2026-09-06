// S12.3.3v2 / S8.2.3: member access is resolution, not membership.
// `obj.m` and its dynamic form `obj["m"]` are one operation and reach the
// type's methods; `in`/`at`/`len` stay on the key domain.

type Point {
    x: int, y: int,
    fn mag() => x + y
    pn shift() { x = x + 1 }
}
type Circle : Point {
    r: int,
    fn area() => r * r
}
let p = <Point x: 3, y: 4>
let c = <Circle x: 1, y: 2, r: 5>

// a: a bare `fn` method is a bound value, and calling it agrees with the
// member-call form (S12.3.4).
'=a='
p.mag()
type(p.mag)
let f = p.mag
f()
call(f, [])

// b: the dynamic form resolves identically (S8.2.1v3).
'=b='
type(p["mag"])
p["mag"]()
p["x"]

// c: the base chain is walked, own methods first.
'=c='
c.mag()
c.area()
type(c["area"])

// d: membership and length stay on the key domain — methods are members of
// the type, never of the value (S8.2.3).
'=d='
len(p)
"mag" at p
"x" at p
"mag" in [for (k at p) k]

// e: a miss is null, not a builtin. The method-eligible builtin tier is a
// call-site rule, so bare access never binds one (LR02-17).
'=e='
type(p.nope)
let m = {a: 1, b: 2}
type(m["len"])
type(m.len)
m.len()
