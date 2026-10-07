// PARKED 2026-10-07 — S11.4.6* enforces a constrained type by its base only, for now, and SO9
// leaves predicate enforcement unowned, so the `that` constraints here are not checked at construction.
// When ruled, give it a golden or move it to the errors harness.
// Test: Object Constraint Fail
// Layer: 2 | Category: negative | Covers: construct object violating that constraint

// Define type with constraint
type Positive {
    value: int
    that value > 0
}

// Violate constraint - should produce runtime error
let bad = <Positive value: -5>
bad.value

// Violate range constraint
type InRange {
    min: int
    max: int
    that min <= max
}

// min > max - should produce error
let bad_range = <InRange min: 10, max: 5>
bad_range.min
