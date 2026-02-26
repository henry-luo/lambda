// Test Math object methods and properties (v3: delegates to Lambda fn_* functions)

// Math.abs
console.log(Math.abs(-5));
console.log(Math.abs(3));

// Math.floor / Math.ceil / Math.round
console.log(Math.floor(4.7));
console.log(Math.ceil(4.2));
console.log(Math.round(4.5));
console.log(Math.round(4.4));

// Math.sqrt
console.log(Math.sqrt(9));
console.log(Math.sqrt(16));

// Math.pow
console.log(Math.pow(2, 10));

// Math.min / Math.max
console.log(Math.min(3, 7));
console.log(Math.max(3, 7));

// Math.sign
console.log(Math.sign(-10));
console.log(Math.sign(0));
console.log(Math.sign(42));

// Math.trunc
console.log(Math.trunc(4.9));
console.log(Math.trunc(-4.9));

// Math.PI (test that it exists and is approximately 3.14159)
let pi = Math.PI;
console.log(Math.floor(pi * 1000));
