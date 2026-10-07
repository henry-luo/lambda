// Test: Object Limits
// Layer: 3 | Category: boundary | Covers: many fields, deep inheritance, many methods

// ===== Many fields =====
type ManyFields {
    f01: int = 1, f02: int = 2, f03: int = 3, f04: int = 4, f05: int = 5
    f06: int = 6, f07: int = 7, f08: int = 8, f09: int = 9, f10: int = 10
    f11: int = 11, f12: int = 12, f13: int = 13, f14: int = 14, f15: int = 15
    f16: int = 16, f17: int = 17, f18: int = 18, f19: int = 19, f20: int = 20
}
let mf = <ManyFields>
mf.f01
mf.f10
mf.f20

// ===== Deep inheritance chain =====
type L0 { val: int = 0 }
type L1 : L0 { val: int = 1 }
type L2 : L1 { val: int = 2 }
type L3 : L2 { val: int = 3 }
type L4 : L3 { val: int = 4 }
type L5 : L4 { val: int = 5 }
let deep = <L5>
deep.val
deep is L0
deep is L3
deep is L5

// ===== Object with many methods =====
type MathObj {
    value: int
    fn add(n: int) => ~.value + n
    fn sub(n: int) => ~.value - n
    fn mul(n: int) => ~.value * n
    fn dbl() => ~.value * 2
    fn sqr() => ~.value * ~.value
    fn neg() => -~.value
    fn is_positive() => ~.value > 0
    fn is_zero() => ~.value == 0
}
let mo = <MathObj value: 7>
mo.add(3)
mo.sub(2)
mo.mul(5)
mo.dbl()
mo.sqr()
mo.neg()
mo.is_positive()
mo.is_zero()

// ===== Large nested object =====
let nested = {
    a: { b: { c: { d: { e: { f: "deep" } } } } }
}
nested.a.b.c.d.e.f

// ===== Collection of objects =====
type Item { id: int, name: string }
let items = for (i in 1 to 10) <Item id: i, name: "item" & str(i)>
len(items)
items[0].name
items[9].name

// ===== Object update chain =====
type Counter { count: int = 0 }
let c0 = <Counter>
let c1 = {c0, count: 1}
let c2 = {c1, count: 2}
let c3 = {c2, count: 3}
c0.count
c1.count
c2.count
c3.count
