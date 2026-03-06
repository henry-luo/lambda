// Array Destructuring Tests

// Basic array destructuring
const [a, b, c] = [1, 2, 3];
console.log("basic:", a, b, c);

// Destructuring with rest element
const [first, ...rest] = [10, 20, 30, 40, 50];
console.log("rest:", first, rest.length, rest[0], rest[1]);

// Destructuring with default values
const [x, y, z] = [100, 200];
console.log("partial:", x, y, z);

// Nested destructuring in for-of
const pairs = [[1, "one"], [2, "two"], [3, "three"]];
for (const [num, name] of pairs) {
    console.log("pair:", num, name);
}

// Destructuring with skip (using more elements than available)
const arr = [7, 8, 9];
const [p, q] = arr;
console.log("subset:", p, q);

// Destructuring with rest in for-of
const rows = [[1, 2, 3, 4], [5, 6, 7, 8]];
for (const [head, ...tail] of rows) {
    console.log("head:", head, "tail length:", tail.length);
}

// Sort with comparator
const nums = [5, 3, 8, 1, 9, 2, 7, 4, 6];
nums.sort(function(a, b) { return a - b; });
console.log("sorted asc:", nums[0], nums[1], nums[2], nums[3], nums[4]);

// Sort descending
const desc = [5, 3, 8, 1, 9];
desc.sort(function(a, b) { return b - a; });
console.log("sorted desc:", desc[0], desc[1], desc[2], desc[3], desc[4]);

// Sort without comparator (default)
const words = ["banana", "apple", "cherry"];
words.sort();
console.log("sorted words:", words[0], words[1], words[2]);

// Math.min/max variadic
console.log("min2:", Math.min(5, 3));
console.log("min3:", Math.min(5, 3, 8));
console.log("min4:", Math.min(5, 3, 8, 1));
console.log("max2:", Math.max(5, 3));
console.log("max3:", Math.max(5, 3, 8));
console.log("max4:", Math.max(5, 3, 8, 1));
console.log("min1:", Math.min(42));
console.log("max1:", Math.max(42));

// Array.fill via method dispatch
const filled = new Array(5);
filled.fill(0);
console.log("filled:", filled[0], filled[1], filled[2], filled.length);
