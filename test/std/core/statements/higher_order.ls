// Test: Higher-Order Functions
// Layer: 2 | Category: statement | Covers: functions as args, returning fns, composition

// ===== Function as argument =====
fn apply_fn(f, x) => f(x)
apply_fn((x) => x * 2, 5)
apply_fn((x) => x & "!", "hello")

// ===== Function returning function =====
fn make_pow(exp: int) => (base: int) => base ** exp
let square = make_pow(2)
let cube = make_pow(3)
square(5)
cube(3)

// ===== Apply twice pattern =====
fn apply_twice(f, x) => f(f(x))
apply_twice((x) => x + 1, 0)
apply_twice((x) => x * 2, 3)
apply_twice((s) => s & s, "ab")

// ===== Compose functions =====
fn compose(f, g) => (x) => f(g(x))
let double_inc = compose((x) => x + 1, (x) => x * 2)
double_inc(5)
double_inc(10)

// ===== Pipeline of functions =====
fn pipeline(fns: lst, value) {
    fns |> reduce((acc, f) => f(acc), value)
}
pipeline([(x) => x + 1, (x) => x * 2, (x) => x - 3], 5)

// ===== Map with function =====
fn transform_all(items, f) => items |> map(f)
transform_all([1, 2, 3], (x) => x * 10)
transform_all(["a", "b", "c"], (s) => s & s)

// ===== Filter with predicate =====
fn select_where(items, predicate) => items |> filter(predicate)
select_where([1, 2, 3, 4, 5, 6], (x) => x > 3)
select_where(["hello", "hi", "hey", "h"], (s) => len(s) > 2)

// ===== Fold/reduce with custom function =====
fn fold(items, f, init) => items |> reduce(f, init)
fold([1, 2, 3, 4], (acc, x) => acc + x, 0)
fold([1, 2, 3, 4], (acc, x) => acc * x, 1)

// ===== Function selection =====
fn get_operation(op: string) => match op {
    case "add": (a, b) => a + b
    case "mul": (a, b) => a * b
    case "sub": (a, b) => a - b
    default: (a, b) => 0
}
let add_fn = get_operation("add")
let mul_fn = get_operation("mul")
add_fn(3, 4)
mul_fn(3, 4)

// ===== Partial application via closure =====
fn partial(f, a) => (b) => f(a, b)
let add5 = partial((a, b) => a + b, 5)
let mul3 = partial((a, b) => a * b, 3)
add5(10)
mul3(7)

// ===== Predicate combinators =====
fn both(p1, p2) => (x) => p1(x) and p2(x)
fn either(p1, p2) => (x) => p1(x) or p2(x)
let positive = (x) => x > 0
let even = (x) => x % 2 == 0
let pos_even = both(positive, even)
pos_even(4)
pos_even(-2)
pos_even(3)
