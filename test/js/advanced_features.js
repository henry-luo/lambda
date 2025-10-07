// JavaScript advanced features test

// Higher-order functions
function createMultiplier(factor) {
    return function(x) {
        return x * factor;
    };
}

var double = createMultiplier(2);
var triple = createMultiplier(3);

// Array operations with functions
var numbers = [1, 2, 3, 4, 5];

// Manual map implementation
function mapArray(arr, fn) {
    var result = [];
    for (var i = 0; i < arr.length; i = i + 1) {
        result[i] = fn(arr[i]);
    }
    return result;
}

// Manual filter implementation
function filterArray(arr, predicate) {
    var result = [];
    var index = 0;
    for (var i = 0; i < arr.length; i = i + 1) {
        if (predicate(arr[i])) {
            result[index] = arr[i];
            index = index + 1;
        }
    }
    return result;
}

// Test higher-order functions
var doubled = mapArray(numbers, double);
var tripled = mapArray(numbers, triple);

// Predicate function
function isEven(n) {
    return n % 2 === 0;
}

var evens = filterArray(numbers, isEven);

// Object methods
var calculator = {
    value: 0,
    add: function(n) {
        this.value = this.value + n;
        return this;
    },
    multiply: function(n) {
        this.value = this.value * n;
        return this;
    },
    getValue: function() {
        return this.value;
    }
};

// Method chaining
calculator.add(5).multiply(3).add(2);
var finalValue = calculator.getValue();

// Closures
function createCounter() {
    var count = 0;
    return function() {
        count = count + 1;
        return count;
    };
}

var counter1 = createCounter();
var counter2 = createCounter();

var count1a = counter1(); // 1
var count1b = counter1(); // 2
var count2a = counter2(); // 1

// Return test results
{
    doubled: doubled,
    tripled: tripled,
    evens: evens,
    finalValue: finalValue,
    count1a: count1a,
    count1b: count1b,
    count2a: count2a
};
