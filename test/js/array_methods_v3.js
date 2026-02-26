// Test array methods (v3: map, filter, reduce, forEach, find, some, every, etc.)

// push and pop
let arr = [1, 2, 3];
arr.push(4);
console.log(arr.length);
let popped = arr.pop();
console.log(popped);
console.log(arr.length);

// indexOf and includes
let nums = [10, 20, 30, 40];
console.log(nums.indexOf(30));
console.log(nums.indexOf(99));
console.log(nums.includes(20));
console.log(nums.includes(99));

// join
console.log(nums.join("-"));

// reverse
let rev = [1, 2, 3].reverse();
console.log(rev.join(","));

// slice
let sliced = [1, 2, 3, 4, 5].slice(1, 3);
console.log(sliced.join(","));

// concat
let merged = [1, 2].concat([3, 4]);
console.log(merged.join(","));

// map
function double(x) { return x * 2; }
let doubled = [1, 2, 3].map(double);
console.log(doubled.join(","));

// filter
function isEven(x) { return x % 2 === 0; }
let evens = [1, 2, 3, 4, 5, 6].filter(isEven);
console.log(evens.join(","));

// reduce
function sum(acc, x) { return acc + x; }
let total = [1, 2, 3, 4].reduce(sum, 0);
console.log(total);

// find
function greaterThan3(x) { return x > 3; }
let found = [1, 2, 3, 4, 5].find(greaterThan3);
console.log(found);

// some and every
function isPositive(x) { return x > 0; }
console.log([1, 2, 3].some(isPositive));
console.log([-1, -2, -3].some(isPositive));
console.log([1, 2, 3].every(isPositive));
console.log([1, -2, 3].every(isPositive));

// forEach (side effects)
function printItem(x) { console.log(x); }
[10, 20, 30].forEach(printItem);
