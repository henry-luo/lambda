// v9 Number static methods, Array.from

// Number.isInteger
console.log("isInteger 5:", Number.isInteger(5));
console.log("isInteger 5.5:", Number.isInteger(5.5));

// Number.isFinite
console.log("isFinite 42:", Number.isFinite(42));

// Number.isNaN
console.log("isNaN 42:", Number.isNaN(42));

// Number.isSafeInteger
console.log("isSafe 42:", Number.isSafeInteger(42));

// Array.from array
let src = [10, 20, 30];
let copy = Array.from(src);
console.log("from array:", copy.join(","));
console.log("from len:", copy.length);

// Array.from string
let chars = Array.from("hi");
console.log("from string:", chars.join(","));
