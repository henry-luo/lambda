// Test math constants: math.pi, math.e
// Test new math functions: math.trunc, math.hypot, math.log1p

// Section 1: Math constants
"1. Math constants"
[math.pi, math.e]

// Section 2: math.trunc - truncate toward zero
"2. math.trunc"
[
    math.trunc(3.7),       // 3
    math.trunc(-3.7),      // -3 (NOT -4 like floor)
    math.trunc(0),         // 0
    math.trunc(2.0),       // 2
    math.trunc(-0.5)       // 0
]

// Section 3: math.hypot - Euclidean distance
"3. math.hypot"
[
    math.hypot(3, 4),      // 5
    math.hypot(0, 0),      // 0
    math.hypot(1, 1),      // sqrt(2)
    math.hypot(5, 12)      // 13
]

// Section 4: math.log1p - ln(1+x), precise for small x
"4. math.log1p"
[
    math.log1p(0),         // 0
    math.log1p(1),         // ln(2)
    math.log1p(-0.5)       // ln(0.5)
]

// Section 5: Vector element-wise
"5. Vector element-wise"
math.trunc([3.7, -3.7, 0.5, -0.5])
math.log1p([0, 1, -0.5])

// Section 6: Typed arguments (native optimization path)
fn typed_trunc(x: float) { math.trunc(x) }
fn typed_hypot(a: float, b: float) { math.hypot(a, b) }
fn typed_log1p(x: float) { math.log1p(x) }

"6. Typed arguments (native path)"
[
    typed_trunc(3.7),
    typed_trunc(-3.7),
    typed_hypot(3.0, 4.0),
    typed_log1p(1.0)
]

// Section 7: Constants in expressions
"7. Constants in expressions"
[
    math.sin(math.pi),         // ~0
    math.cos(math.pi),         // -1
    math.exp(1) - math.e,      // ~0
    2 * math.pi                // tau
]
