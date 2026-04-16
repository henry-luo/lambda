// Nested function call patterns
// Tests deep call chains and higher-order functions

// Simple nested calls
fn double(x) => x * 2
fn add10(x) => x + 10
fn square(x) => x * x

let test1 = square(add10(double(5)))  // ((5*2)+10)^2 = 400

// Higher-order function composition
fn compose(f, g) => (x) => g(f(x))
fn compose3(f, g, h) => (x) => h(g(f(x)))

let f = compose3(double, add10, square)
let test2 = f(5)  // square(add10(double(5)))

// Deeply nested identity calls
fn id(x) => x
let test3 = id(id(id(id(id(id(id(id(id(id(42))))))))))

// Nested calls with collections
let test4 = len(map(filter([1, 2, 3, 4, 5], (x) => x > 2), (x) => x * 2))

// Closure chains
fn make_adder(n) {
    fn add(x) => x + n
    add
}

fn make_multiplier(m) {
    fn mul(x) => x * m
    mul
}

let add5 = make_adder(5)
let mul3 = make_multiplier(3)
let test5 = mul3(add5(10))  // (10 + 5) * 3 = 45

// Nested calls in data structures
let test6 = {
    a: square(5),
    b: add10(square(3)),
    c: square(add10(square(2)))
}

[test1, test2, test3, test4, test5, test6.c]
