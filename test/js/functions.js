// JavaScript functions test

// Function declaration
function add(a, b) {
    return a + b;
}

// Function expression
var multiply = function(x, y) {
    return x * y;
};

// Arrow function
var divide = (a, b) => a / b;

// Function with no parameters
function greet() {
    return "Hello, World!";
}

// Function with conditional logic
function max(a, b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

// Function calling other functions
function calculate(x, y) {
    var sum = add(x, y);
    var product = multiply(x, y);
    return sum + product;
}

// Test function calls
var result1 = add(5, 3);
var result2 = multiply(4, 6);
var result3 = divide(10, 2);
var greeting = greet();
var maximum = max(7, 12);
var calculation = calculate(2, 3);

// Return the final calculation
calculation;
