// Test: Higher-Order Functions
// Layer: 2 | Category: statement | Covers: functions as args, returning fns, composition

// ===== Function as argument =====
fn apply(f, x) => f(x)
apply(fn(x) => x * 2, 5)
apply(fn(x) => x & "!", "hello")

// ===== Function returning function =====
fn make_pow(exp: int) => fn(base: int) => base ** exp
let square = make_pow(2)
let cube = make_pow(3)
square(5)
cube(3)

// ===== Apply twice pattern =====
fn apply_twice(f, x) => f(f(x))
apply_twice(fn(x) => x + 1, 0)
apply_twice(fn(x) => x * 2, 3)
apply_twice(fn(s) => s & s, "ab")

// ===== Compose functions =====
fn compose(f, g) => fn(x) => f(g(x))
let double_inc = compose(fn(x) => x + 1, fn(x) => x * 2)
double_inc(5)
double_inc(10)

// ===== Pipeline of functions =====
fn pipeline(fns: list, value) {
    fns | reduce(fn(acc, f) => f(acc), value)
}
pipeline([fn(x) => x + 1, fn(x) => x * 2, fn(x) => x - 3], 5)

// ===== Map with function =====
fn transform_all(items, f) => items | map(f)
transform_all([1, 2, 3], fn(x) => x * 10)
transform_all(["a", "b", "c"], fn(s) => s & s)

// ===== Filter with predicate =====
fn select_where(items, predicate) => items | filter(predicate)
select_where([1, 2, 3, 4, 5, 6], fn(x) => x > 3)
select_where(["hello", "hi", "hey", "h"], fn(s) => len(s) > 2)

// ===== Fold/reduce with custom function =====
fn fold(items, f, init) => items | reduce(f, init)
fold([1, 2, 3, 4], fn(acc, x) => acc + x, 0)
fold([1, 2, 3, 4], fn(acc, x) => acc * x, 1)

// ===== Function selection =====
fn get_operation(op: string) => match op {
    case "add": fn(a, b) => a + b
    case "mul": fn(a, b) => a * b
    case "sub": fn(a, b) => a - b
    default: fn(a, b) => 0
}
let add_fn = get_operation("add")
let mul_fn = get_operation("mul")
add_fn(3, 4)
mul_fn(3, 4)

// ===== Partial application via closure =====
fn partial(f, a) => fn(b) => f(a, b)
let add5 = partial(fn(a, b) => a + b, 5)
let mul3 = partial(fn(a, b) => a * b, 3)
add5(10)
mul3(7)

// ===== Predicate combinators =====
fn both(p1, p2) => fn(x) => p1(x) and p2(x)
fn either(p1, p2) => fn(x) => p1(x) or p2(x)
let positive = fn(x) => x > 0
let even = fn(x) => x % 2 == 0
let pos_even = both(positive, even)
pos_even(4)
pos_even(-2)
pos_even(3)
