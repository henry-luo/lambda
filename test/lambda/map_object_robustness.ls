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

// 1b: many fields (verify byte offset correctness)
type Wide = {a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int}
let wd: Wide = {a: 1, b: 2, c: 3, d: 4, e: 5, f: 6, g: 7, h: 8}

// 1c: multiple instances share layout but independent data
type Pair = {x: int, y: int}
let p1: Pair = {x: 1, y: 2}
let p2: Pair = {x: 100, y: 200}

// 1d: same field name across different types
type TA = {val: int}
type TB = {val: float}
type TC = {val: string}
type TD = {val: bool}
let ta: TA = {val: 42}
let tb: TB = {val: 3.14}
let tc: TC = {val: "hello"}
let td: TD = {val: true}

// ============================================================
// Section 2: Slow path — untyped maps (runtime name lookup)
// ============================================================

// 2a: plain map field access
let m = {a: 100, b: 200, c: 300}

// 2b: dynamic field on typed map (fallback when field not in shape)
type Small = {a: int}
let sm: Small = {a: 5}

// 2c: bracket notation always uses slow path
let pt: Pair = {x: 10, y: 20}

// ============================================================
// Section 3: Mixed fast/slow in same expression
// ============================================================

// 3a: typed map + untyped map arithmetic
type Pt = {x: int, y: int}
let typed: Pt = {x: 10, y: 20}
let untyped = {x: 3, y: 7}

// 3b: typed map field in system function call
type Dims = {w: int, h: int}
let d: Dims = {w: 15, h: 20}

// ============================================================
// Section 4: Unboxed arithmetic (native ops, no boxing)
// ============================================================

// 4a: int field arithmetic chains
type V3 = {x: int, y: int, z: int}
let v: V3 = {x: 2, y: 3, z: 5}

// 4b: float field arithmetic (no push_d heap alloc)
type FPt = {x: float, y: float}
let fp: FPt = {x: 1.5, y: 2.5}

// 4c: int division (promotes to float)
type Ratio = {a: int, b: int}
let rat: Ratio = {a: 10, b: 3}

// 4d: cross-type field comparison (int fields in condition)
let cmp: Pair = {x: -5, y: 10}
let r1 = (if (cmp.x > 0) "pos" else "non-pos")
let r2 = (if (cmp.y > 0) "pos" else "non-pos")

// 4e: int field equality/inequality

// ============================================================
// Section 5: Negative and zero values
// ============================================================

// 5a: negative ints and floats
type Neg = {i: int, f: float}
let neg: Neg = {i: -100, f: -3.14}

// 5b: zero values for all types
type ZeroAll = {i: int, f: float, s: string, b: bool}
let z: ZeroAll = {i: 0, f: 0.0, s: "", b: false}

// 5c: large int values
type BigVal = {val: i64}
let big: BigVal = {val: 9007199254740992i64}

// ============================================================
// Section 6: Nested typed maps
// ============================================================

// 6a: typed inner map as field
type Inner = {val: int}
type Outer = {name: string, inner: Inner}
let out: Outer = {name: "wrap", inner: {val: 99}}

// 6b: multiple levels — top-level reads fast, chain reads slow
type Coord = {x: float, y: float}
type Location = {label: string, pos: Coord}
let loc: Location = {label: "origin", pos: {x: 0.0, y: 0.0}}

// ============================================================
// Section 7: Function parameter passing (fast path in fn body)
// ============================================================

// 7a: single typed param
fn sum_pt(p: Pt) { p.x + p.y }

// 7b: two typed params of same type
fn add_pts(a: Pt, b: Pt) { [a.x + b.x, a.y + b.y] }

// 7c: typed param + scalar param
fn scale_pt(p: Pt, factor: int) { [p.x * factor, p.y * factor] }

// 7d: function returning value computed from typed param fields
fn manhattan(a: Pt, b: Pt) { abs(a.x - b.x) + abs(a.y - b.y) }

// ============================================================
// Section 8: Object methods (fn/pn) with direct field access
// ============================================================

// 8a: fn method with float field arithmetic
type Vec2 { x: float, y: float; fn length() => math.sqrt(x * x + y * y) }
let vec = <Vec2 x: 3.0, y: 4.0>

// 8b: pn method mutating int field
type Counter { val: int = 0; pn add(n: int) { val = val + n } }

// 8c: multiple pn calls in sequence
type Accum { total: int = 0; pn add(n: int) { total = total + n } }

// 8d: fn method returning list of typed fields
type PtObj { x: int, y: int; fn to_list() => [x, y] }
let po = <PtObj x: 7, y: 8>

// 8e: fn method with parameter + field arithmetic
type Adder { base: int; fn add_to(n: int) => base + n }
let ad = <Adder base: 100>

// ============================================================
// Section 9: Object inheritance + direct access
// ============================================================

// 9a: inherited field read
type Shape { color: string = "black" }
type Circle : Shape { radius: int }
let circ = <Circle color: "red", radius: 5>

// 9b: inherited method call
type Animal { name: string; fn speak() => name ++ " says ..." }
type Dog : Animal { breed: string; fn speak() => name ++ " says woof!" }
let dog = <Dog name: "Rex", breed: "Lab">

// 9c: is type check through hierarchy

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

// 10b: bool toggle mutation
type Toggle {
    on: bool = false;
    pn flip() {
        on = not on
    }
}

// ============================================================
// Section 11: Object with constraints (still works with direct access)
// ============================================================

// 11a: valid constraint
type Positive { val: int that (~ > 0) }

// 11b: invalid constraint

// 11c: object-level constraint
type Range { lo: int, hi: int; that (~.hi > ~.lo) }

// ============================================================
// Section 12: Object defaults + field read
// ============================================================

// 12a: all defaults
type Cfg { host: string = "localhost", port: int = 8080, debug: bool = false }
let cfg = <Cfg>

// 12b: partial override
let cfg2 = <Cfg host: "example.com">

// ============================================================
// Section 13: Map spread/merge and field access
// ============================================================

// 13a: spread typed map into new map
type Base = {x: int, y: int}
let base: Base = {x: 1, y: 2}
let ext = {*:base, z: 3}

// ============================================================
// Section 14: Object update syntax
// ============================================================

// 14a: update object preserving type
type Point2 { x: float, y: float }
let orig = <Point2 x: 1.0, y: 2.0>
let moved = <Point2 *:orig, x: 10.0>

// ============================================================
// Section 15: Map/object is type check
// ============================================================

// 15a: typed map alias — still a map, not an object
type TypedMap = {a: int}
let tm: TypedMap = {a: 1}

// 15b: object type — is map AND is object
let obj = <Counter val: 5>

// ============================================================
// Section 16: Field access on literal (no variable, slow path)
// ============================================================

// ============================================================
// Section 17: Function returning map — dynamic field access
// ============================================================
fn make(a: int, b: int) { {x: a, y: b} }
let made = make(7, 8)

// ============================================================
// Section 18: Variable aliasing a typed map
// ============================================================
let src: Pt = {x: 50, y: 60}
let alias = src

// ============================================================
// Section 19: Object-level constraint with implicit ~.name
// ============================================================

// 19a: object constraint using implicit name (hi, lo instead of ~.hi, ~.lo)
type Range2 { lo: int, hi: int; that (hi > lo) }


// pn mutation methods are exercised from main; functional top-level calls are rejected by E224.
pn emit_value(value: any) { print(format(value, 'mark') ++ "\n") }

pn main() {
    var cnt = <Counter val: 10>
    var ac = <Accum total: 0>
    var wallet = <Wallet balance: 100>
    var t = <Toggle on: false>
    emit_value('=1a=')
    emit_value(w.val)
    emit_value('=1b=')
    emit_value([wd.a, wd.d, wd.h])
    emit_value(wd.a + wd.b + wd.c + wd.d + wd.e + wd.f + wd.g + wd.h)
    emit_value('=1c=')
    emit_value([p1.x, p1.y, p2.x, p2.y])
    emit_value('=1d=')
    emit_value([ta.val, tb.val, tc.val, td.val])
    emit_value('=2a=')
    emit_value(m.a + m.b + m.c)
    emit_value('=2b=')
    emit_value(sm.a)
    emit_value(sm.b)
    emit_value('=2c=')
    emit_value(pt["x"])
    emit_value(pt["y"])
    emit_value('=3a=')
    emit_value(typed.x + untyped.x)
    emit_value(typed.y - untyped.y)
    emit_value('=3b=')
    emit_value(max(d.w, d.h))
    emit_value(min(d.w, d.h))
    emit_value(abs(d.w - d.h))
    emit_value('=4a=')
    emit_value(v.x + v.y + v.z)
    emit_value(v.x * v.y * v.z)
    emit_value((v.x + v.y) * v.z)
    emit_value('=4b=')
    emit_value(fp.x + fp.y)
    emit_value(fp.x * fp.y)
    emit_value('=4c=')
    emit_value(rat.a / rat.b)
    emit_value('=4d=')
    emit_value([r1, r2])
    emit_value('=4e=')
    emit_value(cmp.x == -5)
    emit_value(cmp.y != 0)
    emit_value('=5a=')
    emit_value(neg.i)
    emit_value(neg.f)
    emit_value(neg.i * 2)
    emit_value('=5b=')
    emit_value(z.i)
    emit_value(z.f)
    emit_value(z.b)
    emit_value(z.s == "")
    emit_value('=5c=')
    emit_value(big.val)
    emit_value('=6a=')
    emit_value(out.name)
    emit_value(out.inner.val)
    emit_value('=6b=')
    emit_value(loc.label)
    emit_value(loc.pos)
    emit_value('=7a=')
    emit_value(sum_pt({x: 11, y: 22}))
    emit_value('=7b=')
    emit_value(add_pts({x: 1, y: 2}, {x: 10, y: 20}))
    emit_value('=7c=')
    emit_value(scale_pt({x: 3, y: 4}, 5))
    emit_value('=7d=')
    emit_value(manhattan({x: 0, y: 0}, {x: 3, y: 4}))
    emit_value('=8a=')
    emit_value(vec.length())
    emit_value('=8b=')
    cnt.add(5)
    emit_value(cnt.val)
    emit_value('=8c=')
    ac.add(10)
    ac.add(20)
    ac.add(30)
    emit_value(ac.total)
    emit_value('=8d=')
    emit_value(po.to_list())
    emit_value('=8e=')
    emit_value(ad.add_to(23))
    emit_value('=9a=')
    emit_value(circ.color)
    emit_value(circ.radius)
    emit_value('=9b=')
    emit_value(dog.speak())
    emit_value('=9c=')
    emit_value(dog is Dog)
    emit_value(dog is Animal)
    emit_value(dog is object)
    emit_value('=10a=')
    wallet.deposit(50)
    wallet.withdraw(30)
    emit_value(wallet.balance)
    emit_value('=10b=')
    emit_value(t.on)
    t.flip()
    emit_value(t.on)
    t.flip()
    emit_value(t.on)
    emit_value('=11a=')
    emit_value(<Positive val: 5> is Positive)
    emit_value('=11b=')
    emit_value(<Positive val: -1> is Positive)
    emit_value('=11c=')
    emit_value(<Range lo: 1, hi: 10> is Range)
    emit_value(<Range lo: 10, hi: 1> is Range)
    emit_value('=12a=')
    emit_value([cfg.host, cfg.port, cfg.debug])
    emit_value('=12b=')
    emit_value([cfg2.host, cfg2.port])
    emit_value('=13a=')
    emit_value([ext.x, ext.y, ext.z])
    emit_value('=14a=')
    emit_value([moved.x, moved.y])
    emit_value('=15a=')
    emit_value(tm is map)
    emit_value('=15b=')
    emit_value(obj is map)
    emit_value(obj is object)
    emit_value('=16=')
    emit_value({x: 5, y: 6}.x)
    emit_value({name: "inline"}.name)
    emit_value('=17=')
    emit_value(made.x + made.y)
    emit_value('=18=')
    emit_value(alias.x + alias.y)
    emit_value('=19a=')
    emit_value(<Range2 lo: 1, hi: 10> is Range2)
    emit_value(<Range2 lo: 10, hi: 1> is Range2)
}
