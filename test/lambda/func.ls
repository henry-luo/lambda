pub fn factorial(n: float) float {
    if (n == 0.0) 1.0
    else if (n < 0.0) 0.0
    else n * factorial(n - 1.0)
}

pub fn addition(a, b) {
    a + b
}

pub fn mul(a: float, b: float) float => a * b

pub pi = 3.14159

fn strcat(a: string, b: string) { a ++ b }

factorial(5.0)
strcat("hello", " world")
mul(2, 3.0)

// anonymous function: untyped params
let f = (a, b) => a + b
type(f);  f(1,2);

// anonymous function: no params
let g = () => 42
type(g);  g();

// anonymous function: single untyped param
let double = (x) => x * 2
double(5);

// anonymous function: typed params
let typed_add = (a: int, b: int) => a + b
typed_add(10, 20);

// anonymous function: mixed typed params
let greet = (name: string) => "Hello, " ++ name
greet("world");

// anonymous function as argument (higher-order)
fn apply(f, x) => f(x)
apply((x) => x * 10, 3);

let t = string
t;  "abc" is t;  123 is t;  true is string;

type t2 = string
t2;  "abc" is t2;  123 is t2;

let str = string(123)
str; str is string;

"types :"
type("string");  type(123);  type(true);  type(0.5);  type(null);  
type([123]);  type({a:123});  type(t);  type(factorial);
type(1 to 3);

1 in [1, 2, 3];  5 in [1, 2.5, 3, 5.0];  "abc" in ["abc", "def"];
0 in [1, 2, 3];  6 in [1, 2.5, 3, 5.0];  "ab" in ["abc", "def"];
"a" in "abc";  "d" in "abc"; 

fn const_fn(x_var, y_var) {
    "const_str"
}
const_fn(1, 2)
