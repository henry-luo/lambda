// Float Conversion Test Suite
// Tests cover: float() system function, type conversions, variable assignment, expressions

"===== FLOAT CONVERSION TEST SUITE ====="

'=== Basic float() from int ==='

// int to float
float(0)
float(1)
float(-1)
float(42)
float(-100)
float(2147483647)

'=== float() from float (identity) ==='

// float to float (should return same value)
float(3.14)
float(-2.718)
float(0.0)
float(1.5e10)

'=== float() from int64 ==='

// int64 to float
float(int64(1000000))
float(int64(-999999))

'=== float() from decimal ==='

// decimal to float
float(3.14n)
float(-0.001n)
float(100n)

'=== float() from string ==='

// string to float
float("3.14")
float("-42.5")
float("0")
float("1e10")

'=== float() in variable assignment ==='

// assign to variable and use in expressions
let a = float(5)
a
a + 1.0
a * 2.0
a - 3.0

let b = float(10)
a + b

'=== float() in nested calls ==='

// chained conversions
float(int(3.7))
float(int64(42))
int(float(3))

'=== float() with user-defined typed functions ==='

// user functions with float params/return still work
fn double_it(x: float) float { x * 2.0 }
double_it(3.0)
double_it(float(5))

fn add_floats(a: float, b: float) float { a + b }
add_floats(1.5, 2.5)
add_floats(float(1), float(2))

'=== type() of float() result ==='

type(float(42))
type(float("3.14"))
type(float(3.14))
