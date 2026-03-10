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
