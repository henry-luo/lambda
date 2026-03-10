// Comprehensive map/object optimization robustness tests
// Covers: fast path (direct struct), slow path (runtime lookup),
// edge cases, negative cases, mixed types, nested maps, objects,
// inheritance, methods, mutation, and combination scenarios.
//
// Each sub-test is labeled with '=Xa=' to separate output sections.

// ============================================================
// Section 1: Fast path — typed map with direct struct access
// ============================================================

// 1a: single-field map
type Wrapper = {val: int}
let w: Wrapper = {val: 42}
'=1a='
w.val

// 1b: many fields (verify byte offset correctness)
type Wide = {a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int}
let wd: Wide = {a: 1, b: 2, c: 3, d: 4, e: 5, f: 6, g: 7, h: 8}
'=1b='
[wd.a, wd.d, wd.h]
wd.a + wd.b + wd.c + wd.d + wd.e + wd.f + wd.g + wd.h

// 1c: multiple instances share layout but independent data
type Pair = {x: int, y: int}
let p1: Pair = {x: 1, y: 2}
let p2: Pair = {x: 100, y: 200}
'=1c='
[p1.x, p1.y, p2.x, p2.y]

// 1d: same field name across different types
type TA = {val: int}
type TB = {val: float}
type TC = {val: string}
type TD = {val: bool}
let ta: TA = {val: 42}
let tb: TB = {val: 3.14}
let tc: TC = {val: "hello"}
let td: TD = {val: true}
'=1d='
[ta.val, tb.val, tc.val, td.val]

// ============================================================
// Section 2: Slow path — untyped maps (runtime name lookup)
// ============================================================

// 2a: plain map field access
let m = {a: 100, b: 200, c: 300}
'=2a='
m.a + m.b + m.c

// 2b: dynamic field on typed map (fallback when field not in shape)
type Small = {a: int}
let sm: Small = {a: 5}
'=2b='
sm.a
sm.b

// 2c: bracket notation always uses slow path
let pt: Pair = {x: 10, y: 20}
'=2c='
pt["x"]
pt["y"]

// ============================================================
// Section 3: Mixed fast/slow in same expression
// ============================================================

// 3a: typed map + untyped map arithmetic
type Pt = {x: int, y: int}
let typed: Pt = {x: 10, y: 20}
let untyped = {x: 3, y: 7}
'=3a='
typed.x + untyped.x
typed.y - untyped.y

// 3b: typed map field in system function call
type Dims = {w: int, h: int}
let d: Dims = {w: 15, h: 20}
'=3b='
max(d.w, d.h)
min(d.w, d.h)
abs(d.w - d.h)

// ============================================================
// Section 4: Unboxed arithmetic (native ops, no boxing)
// ============================================================

// 4a: int field arithmetic chains
type V3 = {x: int, y: int, z: int}
let v: V3 = {x: 2, y: 3, z: 5}
'=4a='
v.x + v.y + v.z
v.x * v.y * v.z
(v.x + v.y) * v.z

// 4b: float field arithmetic (no push_d heap alloc)
type FPt = {x: float, y: float}
let fp: FPt = {x: 1.5, y: 2.5}
'=4b='
fp.x + fp.y
fp.x * fp.y

// 4c: int division (promotes to float)
type Ratio = {a: int, b: int}
let rat: Ratio = {a: 10, b: 3}
'=4c='
rat.a / rat.b

// 4d: cross-type field comparison (int fields in condition)
let cmp: Pair = {x: -5, y: 10}
let r1 = (if (cmp.x > 0) "pos" else "non-pos")
let r2 = (if (cmp.y > 0) "pos" else "non-pos")
'=4d='
[r1, r2]

// 4e: int field equality/inequality
'=4e='
cmp.x == -5
cmp.y != 0

// ============================================================
// Section 5: Negative and zero values
// ============================================================

// 5a: negative ints and floats
type Neg = {i: int, f: float}
let neg: Neg = {i: -100, f: -3.14}
'=5a='
neg.i
neg.f
neg.i * 2

// 5b: zero values for all types
type ZeroAll = {i: int, f: float, s: string, b: bool}
let z: ZeroAll = {i: 0, f: 0.0, s: "", b: false}
'=5b='
z.i
z.f
z.b
z.s == ""

// 5c: large int values
type BigVal = {val: int}
let big: BigVal = {val: 9007199254740992}
'=5c='
big.val

// ============================================================
// Section 6: Nested typed maps
// ============================================================

// 6a: typed inner map as field
type Inner = {val: int}
type Outer = {name: string, inner: Inner}
let out: Outer = {name: "wrap", inner: {val: 99}}
'=6a='
out.name
out.inner.val

// 6b: multiple levels — top-level reads fast, chain reads slow
type Coord = {x: float, y: float}
type Location = {label: string, pos: Coord}
let loc: Location = {label: "origin", pos: {x: 0.0, y: 0.0}}
'=6b='
loc.label
loc.pos

// ============================================================
// Section 7: Function parameter passing (fast path in fn body)
// ============================================================

// 7a: single typed param
fn sum_pt(p: Pt) { p.x + p.y }
'=7a='
sum_pt({x: 11, y: 22})

// 7b: two typed params of same type
fn add_pts(a: Pt, b: Pt) { [a.x + b.x, a.y + b.y] }
'=7b='
add_pts({x: 1, y: 2}, {x: 10, y: 20})

// 7c: typed param + scalar param
fn scale_pt(p: Pt, factor: int) { [p.x * factor, p.y * factor] }
'=7c='
scale_pt({x: 3, y: 4}, 5)

// 7d: function returning value computed from typed param fields
fn manhattan(a: Pt, b: Pt) { abs(a.x - b.x) + abs(a.y - b.y) }
'=7d='
manhattan({x: 0, y: 0}, {x: 3, y: 4})

// ============================================================
// Section 8: Object methods (fn/pn) with direct field access
// ============================================================

// 8a: fn method with float field arithmetic
type Vec2 { x: float, y: float; fn length() => math.sqrt(x * x + y * y) }
let vec = <Vec2 x: 3.0, y: 4.0>
'=8a='
vec.length()

// 8b: pn method mutating int field
type Counter { val: int = 0; pn add(n: int) { val = val + n } }
let cnt = <Counter val: 10>
'=8b='
cnt.add(5)
cnt.val

// 8c: multiple pn calls in sequence
type Accum { total: int = 0; pn add(n: int) { total = total + n } }
let ac = <Accum total: 0>
'=8c='
ac.add(10)
ac.add(20)
ac.add(30)
ac.total

// 8d: fn method returning list of typed fields
type PtObj { x: int, y: int; fn to_list() => [x, y] }
let po = <PtObj x: 7, y: 8>
'=8d='
po.to_list()

// 8e: fn method with parameter + field arithmetic
type Adder { base: int; fn add_to(n: int) => base + n }
let ad = <Adder base: 100>
'=8e='
ad.add_to(23)

// ============================================================
// Section 9: Object inheritance + direct access
// ============================================================

// 9a: inherited field read
type Shape { color: string = "black" }
type Circle : Shape { radius: int }
let circ = <Circle color: "red", radius: 5>
'=9a='
circ.color
circ.radius

// 9b: inherited method call
type Animal { name: string; fn speak() => name ++ " says ..." }
type Dog : Animal { breed: string; fn speak() => name ++ " says woof!" }
let dog = <Dog name: "Rex", breed: "Lab">
'=9b='
dog.speak()

// 9c: is type check through hierarchy
'=9c='
dog is Dog
dog is Animal
dog is object

// ============================================================
// Section 10: Object mutation (pn) and read-back
// ============================================================

// 10a: int mutation and subsequent read
type Wallet {
    balance: int = 0;
    pn deposit(n: int) {
        balance = balance + n
    }
    pn withdraw(n: int) {
        balance = balance - n
    }
}
let wallet = <Wallet balance: 100>
'=10a='
wallet.deposit(50)
wallet.withdraw(30)
wallet.balance

// 10b: bool toggle mutation
type Toggle {
    on: bool = false;
    pn flip() {
        on = not on
    }
}
let t = <Toggle on: false>
'=10b='
t.on
t.flip()
t.on
t.flip()
t.on

// ============================================================
// Section 11: Object with constraints (still works with direct access)
// ============================================================

// 11a: valid constraint
type Positive { val: int that (~ > 0) }
'=11a='
<Positive val: 5> is Positive

// 11b: invalid constraint
'=11b='
<Positive val: -1> is Positive

// 11c: object-level constraint
type Range { lo: int, hi: int; that (~.hi > ~.lo) }
'=11c='
<Range lo: 1, hi: 10> is Range
<Range lo: 10, hi: 1> is Range

// ============================================================
// Section 12: Object defaults + field read
// ============================================================

// 12a: all defaults
type Cfg { host: string = "localhost", port: int = 8080, debug: bool = false }
let cfg = <Cfg>
'=12a='
[cfg.host, cfg.port, cfg.debug]

// 12b: partial override
let cfg2 = <Cfg host: "example.com">
'=12b='
[cfg2.host, cfg2.port]

// ============================================================
// Section 13: Map spread/merge and field access
// ============================================================

// 13a: spread typed map into new map
type Base = {x: int, y: int}
let base: Base = {x: 1, y: 2}
let ext = {base, z: 3}
'=13a='
[ext.x, ext.y, ext.z]

// ============================================================
// Section 14: Object update syntax
// ============================================================

// 14a: update object preserving type
type Point2 { x: float, y: float }
let orig = <Point2 x: 1.0, y: 2.0>
let moved = <Point2 orig, x: 10.0>
'=14a='
[moved.x, moved.y]

// ============================================================
// Section 15: Map/object is type check
// ============================================================

// 15a: typed map alias — still a map, not an object
type TypedMap = {a: int}
let tm: TypedMap = {a: 1}
'=15a='
tm is map

// 15b: object type — is map AND is object
let obj = <Counter val: 5>
'=15b='
obj is map
obj is object

// ============================================================
// Section 16: Field access on literal (no variable, slow path)
// ============================================================
'=16='
{x: 5, y: 6}.x
{name: "inline"}.name

// ============================================================
// Section 17: Function returning map — dynamic field access
// ============================================================
fn make(a: int, b: int) { {x: a, y: b} }
let made = make(7, 8)
'=17='
made.x + made.y

// ============================================================
// Section 18: Variable aliasing a typed map
// ============================================================
let src: Pt = {x: 50, y: 60}
let alias = src
'=18='
alias.x + alias.y

// ============================================================
// Section 19: Object-level constraint with implicit ~.name
// ============================================================

// 19a: object constraint using implicit name (hi, lo instead of ~.hi, ~.lo)
type Range2 { lo: int, hi: int; that (hi > lo) }
'=19a='
<Range2 lo: 1, hi: 10> is Range2
<Range2 lo: 10, hi: 1> is Range2
