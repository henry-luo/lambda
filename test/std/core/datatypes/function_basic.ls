// Test: Function Basic
// Layer: 1 | Category: datatype | Covers: function as value, closures, higher-order

// ===== Function definition =====
fn add(a, b) => a + b
add(3, 4)

// ===== Function as value =====
type(add)
let f = add
f(10, 20)

// ===== Arrow function =====
fn square(x) => x * x
square(5)

// ===== Function with type annotations =====
fn greet(name: string) string => "Hello, " ++ name
greet("Lambda")

// ===== Function returning function =====
fn make_adder(n) {
    fn inner(x) => x + n
    inner
}
let add10 = make_adder(10)
add10(5)

// ===== Higher-order function =====
fn apply_twice(f, x) => f(f(x))
fn inc(x) => x + 1
apply_twice(inc, 5)

// ===== Named arguments =====
fn create(name: string, age: int) => {name: name, age: age}
create(name: "Alice", age: 30)

// ===== Optional parameter =====
fn greet2(name: string, prefix: string?) => (prefix or "Hello") ++ ", " ++ name
greet2("Alice")
greet2("Alice", "Hi")

// ===== Function with default =====
fn repeat(s: string, n: int = 3) => [for (i in 1 to n) s]
repeat("hi")
repeat("hi", 2)

// ===== Closure captures =====
fn make_counter(start) {
    fn counter(x) => x + start
    counter
}
let c = make_counter(100)
c(1)
c(2)
c(3)
