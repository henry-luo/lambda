// Phase 5f: Sort, Math variadic, fill, and destructuring patterns
// Complements destructuring.js with additional coverage.

// --- Sort with arrow function comparator ---
const nums = [5, 3, 8, 1, 9, 2];
nums.sort((a, b) => a - b);
console.log("arrow sort:", nums[0], nums[1], nums[2], nums[3], nums[4], nums[5]);

// --- Sort with closure comparator ---
function makeSorter(ascending) {
    if (ascending) {
        return function(a, b) { return a - b; };
    } else {
        return function(a, b) { return b - a; };
    }
}
const vals = [4, 2, 7, 1, 5];
vals.sort(makeSorter(true));
console.log("closure sort:", vals[0], vals[1], vals[2], vals[3], vals[4]);

const vals2 = [4, 2, 7, 1, 5];
vals2.sort(makeSorter(false));
console.log("closure desc:", vals2[0], vals2[1], vals2[2], vals2[3], vals2[4]);

// --- Default sort: JS spec lexicographic ---
const mixed = [10, 9, 80, 7];
mixed.sort();
console.log("lex sort:", mixed[0], mixed[1], mixed[2], mixed[3]);

// --- Sort strings default ---
const fruits = ["cherry", "apple", "banana", "date"];
fruits.sort();
console.log("str sort:", fruits[0], fruits[1], fruits[2], fruits[3]);

// --- Math.min/max with varying arity ---
console.log("min1:", Math.min(42));
console.log("min2:", Math.min(5, 3));
console.log("min3:", Math.min(5, 3, 8));
console.log("min4:", Math.min(5, 3, 8, 1));
console.log("max1:", Math.max(42));
console.log("max2:", Math.max(5, 3));
console.log("max3:", Math.max(5, 3, 8));
console.log("max4:", Math.max(5, 3, 8, 1));
console.log("min0:", Math.min());
console.log("max0:", Math.max());

// --- Array.fill ---
const zeros = new Array(4);
zeros.fill(0);
console.log("fill:", zeros[0], zeros[1], zeros[2], zeros[3]);

// --- Destructuring with rest and computation ---
const [head, ...tail] = [10, 20, 30, 40, 50];
let tailSum = 0;
for (let i = 0; i < tail.length; i = i + 1) {
    tailSum = tailSum + tail[i];
}
console.log("rest sum:", head, tailSum);

// --- For-of destructuring with accumulation ---
const pairs = [[1, 10], [2, 20], [3, 30]];
let dotProduct = 0;
for (const [a, b] of pairs) {
    dotProduct = dotProduct + a * b;
}
console.log("dot:", dotProduct);

// --- Multiple sequential destructurings ---
const [x1, y1] = [1, 2];
const [x2, y2] = [3, 4];
console.log("multi destr:", x1 + x2, y1 + y2);

// --- Destructuring from function return ---
function getCoords() {
    return [42, 99];
}
const [cx, cy] = getCoords();
console.log("fn destr:", cx, cy);

// --- For-of destructuring with rest ---
const rows = [[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12]];
for (const [first, ...others] of rows) {
    console.log("row:", first, others.length);
}

// --- Sort stability: objects by key ---
const items = [30, 10, 20, 10, 30, 20];
items.sort((a, b) => a - b);
console.log("stable:", items[0], items[1], items[2], items[3], items[4], items[5]);
