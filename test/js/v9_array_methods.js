// v9 Array methods: splice, shift, unshift, lastIndexOf, flatMap, reduceRight, at, toString

// splice — remove elements
let arr1 = [1, 2, 3, 4, 5];
let removed = arr1.splice(1, 2);
console.log("splice remove:", arr1.join(","), "removed:", removed.join(","));

// splice — insert elements
let arr2 = [1, 2, 5];
arr2.splice(2, 0, 3, 4);
console.log("splice insert:", arr2.join(","));

// splice — replace elements
let arr3 = [1, 2, 3, 4, 5];
let replaced = arr3.splice(1, 2, 20, 30);
console.log("splice replace:", arr3.join(","), "replaced:", replaced.join(","));

// splice — negative start
let arr4 = [1, 2, 3, 4, 5];
arr4.splice(-2, 1);
console.log("splice neg:", arr4.join(","));

// shift — remove first element
let arr5 = [10, 20, 30];
let first = arr5.shift();
console.log("shift:", first, "remaining:", arr5.join(","));

// unshift — add to front
let arr6 = [3, 4, 5];
let newLen = arr6.unshift(1, 2);
console.log("unshift:", arr6.join(","), "len:", newLen);

// lastIndexOf
let arr7 = [1, 2, 3, 2, 1];
console.log("lastIndexOf 2:", arr7.lastIndexOf(2));
console.log("lastIndexOf 9:", arr7.lastIndexOf(9));

// flatMap
function dupArr(x) { return [x, x * 2]; }
let arr8 = [1, 2, 3].flatMap(dupArr);
console.log("flatMap:", arr8.join(","));

// reduceRight — sum from right
function sumRight(acc, x) { return acc + x; }
let result = [1, 2, 3, 4].reduceRight(sumRight, 0);
console.log("reduceRight:", result);

// at — positive and negative indexing
let arr9 = [10, 20, 30, 40, 50];
console.log("at 0:", arr9.at(0));
console.log("at -1:", arr9.at(-1));
console.log("at -2:", arr9.at(-2));

// toString
let arr10 = [1, 2, 3];
console.log("toString:", arr10.toString());
