// Function definitions
fn add(a: int, b: int) -> int { a + b }
fn multiply(x, y) => x * y;
fn factorial(n: int) -> int {
    if (n <= 1) 1 else n * factorial(n - 1)
}

// Anonymous functions
let square = (x) => x * x;
let cube = (x) => x * x * x;

// Function calls
add(1, 2);
multiply(3, 4);
factorial(5);
square(6);
