// Test: Arrow Functions
// Layer: 2 | Category: statement | Covers: arrow syntax, closures, inline use

// ===== Basic arrow =====
let dbl = (x) => x * 2
dbl(5)
dbl(0)

// ===== Single param (no parens needed in Lambda) =====
let inc = (x) => x + 1
inc(10)
inc(-1)

// ===== Multi-param arrow =====
let adder = (a, b) => a + b
adder(3, 7)
adder(-1, 1)

// ===== Arrow with type annotations =====
let typed_add = (a: int, b: int) => a + b
typed_add(10, 20)

// ===== Arrow returning string =====
let greet = (name: string) => "Hi, " & name
greet("Lambda")

// ===== Inline in map =====
[1, 2, 3, 4, 5] | map((x) => x * x)

// ===== Inline in filter =====
[1, 2, 3, 4, 5, 6] | filter((x) => x % 2 == 0)

// ===== Inline in reduce =====
[1, 2, 3, 4] | reduce((acc, x) => acc + x)

// ===== Arrow as argument =====
fn apply(f, x) => f(x)
apply((x) => x * 10, 5)

// ===== Nested arrows =====
let make_adder = (n) => (x) => x + n
let add5 = make_adder(5)
add5(10)
add5(0)

// ===== Arrow with block body =====
let process = (x) => {
    let doubled = x * 2
    let tripled = x * 3
    [doubled, tripled]
}
process(5)

// ===== Arrow composition =====
let compose = (f, g) => (x) => f(g(x))
let double_then_inc = compose((x) => x + 1, (x) => x * 2)
double_then_inc(5)
double_then_inc(3)

// ===== Arrow in let =====
let transform = [1, 2, 3] | map((x) => x + 10)
transform

// ===== Arrow with boolean =====
let is_positive = (x) => x > 0
is_positive(5)
is_positive(-3)
is_positive(0)
