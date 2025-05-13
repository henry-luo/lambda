fn factorial(n: float) {
    if (n == 0.0) 1.0
    else if (n < 0.0) 0.0
    else n * factorial(n - 1.0)
}

fn strcat(a: string, b: string) { a + b }

strcat("hello", " world")
factorial(5.0)

// fm: all(), any()
// numbers = [2, 5, 7, 9]
// all(n > 0 for n in numbers) 
// any(n % 5 == 0 for n in numbers) 