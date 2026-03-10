// Test: That Constraints
// Layer: 2 | Category: statement | Covers: field constraints, object constraints, that filter

// ===== Field constraint =====
type PositiveInt {
    value: int
    that value > 0
}
let p = <PositiveInt value: 5>
p.value

// ===== Multiple field constraints =====
type BoundedInt {
    value: int
    that value >= 0
    that value <= 100
}
let b = <BoundedInt value: 50>
b.value

// ===== Object-level constraint =====
type Range {
    min: int
    max: int
    that min <= max
}
let r = <Range min: 1, max: 10>
r.min
r.max

// ===== String constraint =====
type NonEmpty {
    text: string
    that len(text) > 0
}
let ne = <NonEmpty text: "hello">
ne.text

// ===== Constraint with method =====
type Percentage {
    value: float
    that value >= 0.0 and value <= 100.0
    fn normalized() => ~.value / 100.0
}
let pct = <Percentage value: 75.0>
pct.value
pct.normalized()

// ===== That in type definition (filter-like) =====
let positives = [1, -2, 3, -4, 5] | filter((x) => x > 0)
positives

// ===== Constraint inheritance =====
type Base { value: int, that value > 0 }
type Extended : Base { label: string }
let ext = <Extended value: 10, label: "ten">
ext.value
ext.label
ext is Extended
ext is Base
