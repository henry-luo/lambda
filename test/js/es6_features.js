// ES6+ Features Test

// Template literals
var name = "JavaScript";
var version = 6;
var message = `Welcome to ${name} ES${version}!`;
var multiline = `This is a
multi-line
template literal`;

// Destructuring assignment
var numbers = [1, 2, 3, 4, 5];
// var [first, second, ...rest] = numbers; // TODO: Implement destructuring

// Arrow functions with template literals
var createGreeting = (name) => `Hello, ${name}!`;
var greeting = createGreeting("World");

// Classes
class Calculator {
    constructor(initial) {
        this.value = initial || 0;
    }
    
    add(n) {
        this.value = this.value + n;
        return this;
    }
    
    multiply(n) {
        this.value = this.value * n;
        return this;
    }
    
    getValue() {
        return this.value;
    }
}

// Class usage
var calc = new Calculator(10);
calc.add(5).multiply(2);
var result = calc.getValue();

// Enhanced object literals
var x = 10;
var y = 20;
var point = {
    x: x,
    y: y,
    distance: function() {
        return Math.sqrt(this.x * this.x + this.y * this.y);
    }
};

// Array methods
var data = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

// Map function
var doubled = data.map(function(x) { return x * 2; });

// Filter function
var evens = data.filter(function(x) { return x % 2 === 0; });

// Reduce function
var sum = data.reduce(function(acc, x) { return acc + x; }, 0);

// Return test results
{
    message: message,
    multiline: multiline,
    greeting: greeting,
    calculatorResult: result,
    point: point,
    doubled: doubled,
    evens: evens,
    sum: sum
};
