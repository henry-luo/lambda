// Basic function declaration and call
function add(a, b) {
    return a + b;
}

var result = add(3, 4);
console.log(result);

// Function with no parameters
function greet() {
    return "Hello";
}
console.log(greet());

// Nested function calls
function square(x) {
    return x * x;
}

console.log(add(square(2), square(3)));
