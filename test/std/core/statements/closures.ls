// Test: Closures
// Layer: 2 | Category: statement | Covers: capture, nesting, currying, practical patterns

// ===== Basic closure =====
fn make_counter(start: int) {
    let count = start
    fn() => count
}
let get_count = make_counter(10)
get_count()

// ===== Closure over parameter =====
fn make_adder(n: int) => fn(x: int) => x + n
let add10 = make_adder(10)
add10(5)
add10(20)

// ===== Multiple closures sharing environment =====
fn make_greeter(prefix: string) {
    fn greet(name: string) => prefix & ", " & name & "!"
    greet
}
let hello = make_greeter("Hello")
let hi = make_greeter("Hi")
hello("Alice")
hi("Bob")

// ===== Currying pattern =====
fn curry_add(a: int) => fn(b: int) => a + b
let add3 = curry_add(3)
add3(7)
add3(10)

// ===== Nested closures =====
fn outer(x: int) {
    fn middle(y: int) {
        fn inner(z: int) => x + y + z
        inner
    }
    middle
}
let m = outer(1)
let i = m(2)
i(3)

// ===== Closure in collection pipeline =====
fn make_multiplier(factor: int) => fn(x: int) => x * factor
let times3 = make_multiplier(3)
[1, 2, 3, 4, 5] | map(times3)

// ===== Closure with captured collection =====
fn make_lookup(data: map) => fn(key: string) => data.(key)
let lookup = make_lookup({name: "Alice", age: 30})
lookup("name")
lookup("age")

// ===== Closure as predicate =====
fn greater_than(threshold: int) => fn(x: int) => x > threshold
[1, 5, 10, 15, 20] | filter(greater_than(8))

// ===== Function factory =====
fn make_formatter(prefix: string, suffix: string) =>
    fn(text: string) => prefix & text & suffix
let bracket = make_formatter("[", "]")
let paren = make_formatter("(", ")")
bracket("hello")
paren("world")

// ===== Closure preserving environment =====
fn make_range_checker(low: int, high: int) =>
    fn(x: int) => x >= low and x <= high
let in_range = make_range_checker(1, 10)
in_range(5)
in_range(0)
in_range(10)
in_range(11)
