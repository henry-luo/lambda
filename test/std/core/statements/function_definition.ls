// Test: Function Definition
// Layer: 3 | Category: statement | Covers: fn, arrow, closure, recursion

fn add(a: int, b: int) int => a + b
add(3, 4)

fn square(n: int) int => n * n
square(5)

fn factorial(n: int) int {
    if (n == 0) 1 else n * factorial(n - 1)
}
factorial(5)

fn greet(name: string) string => "Hello " ++ name
greet("World")

let double = (x: int) => x * 2
double(10)

fn apply(f, x: int) => f(x)
apply((x) => x + 100, 5)

fn make_adder(n: int) => (x: int) => x + n
let add10 = make_adder(10)
add10(5)

fn fib(n: int) int {
    if (n == 0) 0
    else if (n == 1) 1
    else fib(n - 1) + fib(n - 2)
}
fib(10)
