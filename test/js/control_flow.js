// JavaScript control flow test

// If statements
var x = 10;
var y = 5;

if (x > y) {
    var message = "x is greater";
} else {
    var message = "y is greater or equal";
}

// While loop
var count = 0;
var sum = 0;
while (count < 5) {
    sum = sum + count;
    count = count + 1;
}

// For loop
var total = 0;
for (var i = 0; i < 10; i = i + 1) {
    total = total + i;
}

// Nested control flow
function fibonacci(n) {
    if (n <= 1) {
        return n;
    } else {
        return fibonacci(n - 1) + fibonacci(n - 2);
    }
}

// Ternary operator
var max = x > y ? x : y;

// Break and continue in loops
var evenSum = 0;
for (var j = 0; j < 20; j = j + 1) {
    if (j % 2 === 1) {
        continue; // Skip odd numbers
    }
    if (j > 15) {
        break; // Stop at 15
    }
    evenSum = evenSum + j;
}

// Test the fibonacci function
var fib5 = fibonacci(5);

// Return the results
{
    sum: sum,
    total: total,
    max: max,
    evenSum: evenSum,
    fib5: fib5
};
