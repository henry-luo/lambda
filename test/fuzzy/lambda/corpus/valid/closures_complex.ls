// Complex closure patterns
// Tests multi-level closures, closure composition, and advanced patterns

// Multi-level closure with multiple captures
fn make_calculator(base) {
    let multiplier = 10
    fn calc(op) {
        fn apply(x) {
            if (op == "add") x + base
            else if (op == "mul") x * base * multiplier
            else x
        }
        apply
    }
    calc
}

let calc = make_calculator(5)
let adder = calc("add")
let multiplier_fn = calc("mul")
let test1 = adder(10) + multiplier_fn(2)

// Closure composition
fn compose_closures(f, g) {
    fn composed(x) => g(f(x))
    composed
}

fn make_adder(n) {
    fn add(x) => x + n
    add
}

let add5 = make_adder(5)
let add10 = make_adder(10)
let add15 = compose_closures(add5, add10)
let test2 = add15(20)

// Array of closures
let closures = [
    make_adder(1),
    make_adder(2),
    make_adder(3)
]

let test3 = closures[0](10) + closures[1](10) + closures[2](10)

// Closure with conditional logic
fn make_conditional_closure(threshold) {
    fn check(x) => if (x > threshold) x * 2 else x / 2
    check
}

let check10 = make_conditional_closure(10)
let test4 = check10(15) + check10(5)

// Closure returning map of closures
fn make_ops(base) {
    {
        add: (x) => x + base,
        mul: (x) => x * base,
        sub: (x) => x - base
    }
}

let ops = make_ops(10)
let test5 = ops.add(5) + ops.mul(3) - ops.sub(2)

// Curried functions (3 levels)
fn curry3(a) {
    fn b_curry(b) {
        fn c_curry(c) => a + b + c
        c_curry
    }
    b_curry
}

let test6 = curry3(1)(2)(3)

// Closure capturing another closure
fn outer(a) {
    let inner_closure = make_adder(a)
    fn use_closure(x) => inner_closure(x) * 2
    use_closure
}

let test7 = outer(5)(10)

[test1, test2, test3, test4, test5, test6, test7]
