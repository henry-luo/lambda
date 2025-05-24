fn factorial(n: float) :float {
    if (n == 0.0) 1.0
    else if (n < 0.0) 0.0
    else n * factorial(n - 1.0)
}

fn strcat(a: string, b: string) { a + b }

fn mul(a: float, b: float) :float => a * b

factorial(5.0)
strcat("hello", " world")
mul(2, 3.0)

let f = fn(a, b) { a + b }
f;  f(1,2);

let t = string
t;  "abc" is t;  123 is t;  true is string;

type t2 = string
t2;  "abc" is t2;  123 is t2;

let str = string(123)
str; str is string;

type("string");  type(123);  type(true);  type(0.5);  type(null);  type([123]);  type({a:123});  type(t);  type(factorial);

1 in [1, 2, 3];  5 in [1, 2.5, 3, 5.0];  "abc" in ["abc", "def"];
0 in [1, 2, 3];  6 in [1, 2.5, 3, 5.0];  "ab" in ["abc", "def"];
"a" in "abc";  "d" in "abc"; 