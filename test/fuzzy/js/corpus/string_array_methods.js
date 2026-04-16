// Edge case: string and array methods - stress test
"abc".charAt(0);
"abc".charAt(-1);
"abc".charAt(999);
"abc".charCodeAt(0);
"abc"[Symbol.iterator];

"hello world".indexOf("world");
"hello world".indexOf("xyz");
"hello world".lastIndexOf("l");
"hello world".includes("world");
"hello world".startsWith("hello");
"hello world".endsWith("world");

"abc".repeat(0);
"abc".repeat(1);
"abc".repeat(3);
try { "abc".repeat(-1); } catch(e) {}

"  hello  ".trim();
"  hello  ".trimStart();
"  hello  ".trimEnd();

"abc".padStart(6, "0");
"abc".padEnd(6, ".");
"abc".padStart(2, "0"); // shorter than original

"hello".slice(1, 3);
"hello".slice(-3);
"hello".substring(1, 3);

"a,b,c".split(",");
"abc".split("");
"abc".split("", 2);

"Hello World".toLowerCase();
"Hello World".toUpperCase();

"abcabc".replace("a", "X");
"abcabc".replaceAll("a", "X");

// Array methods chain
[3, 1, 4, 1, 5, 9].sort().reverse().join("-");

[1, 2, 3].map(function(x) { return x * 2; });
[1, 2, 3, 4].filter(function(x) { return x % 2 === 0; });
[1, 2, 3].reduce(function(a, b) { return a + b; }, 0);
[1, 2, 3].reduceRight(function(a, b) { return a + b; }, 0);

[1, 2, 3].every(function(x) { return x > 0; });
[1, 2, 3].some(function(x) { return x > 2; });
[1, 2, 3].find(function(x) { return x > 1; });
[1, 2, 3].findIndex(function(x) { return x > 1; });

[1, 2, 3].includes(2);
[1, 2, 3].indexOf(2);
[1, 2, 3].lastIndexOf(2);

// Edge: reduce on empty array (should throw)
try { [].reduce(function(a, b) { return a + b; }); } catch(e) {}

// Flat
[1, [2, [3]]].flat();
[1, [2, [3]]].flat(Infinity);

// Array.from
Array.from("abc");
Array.from({length: 3}, function(_, i) { return i; });
Array.isArray([]);
Array.isArray("not array");

// Splice / slice
var arr = [1, 2, 3, 4, 5];
arr.slice(1, 3);
arr.splice(1, 2);
arr;

// Push / pop / shift / unshift
var arr2 = [];
arr2.push(1, 2, 3);
arr2.pop();
arr2.shift();
arr2.unshift(0);
arr2;
