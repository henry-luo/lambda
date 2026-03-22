// Test 1: Basic regex.test() - match
var re = /hello/;
console.log(re.test("hello world"));

// Test 2: regex.test() - no match
console.log(re.test("goodbye"));

// Test 3: Case-insensitive flag
var rei = /hello/i;
console.log(rei.test("HELLO WORLD"));

// Test 4: regex.exec() with captures
var re2 = /(\d+)-(\d+)/;
var m = re2.exec("date: 2024-01");
console.log(m[0]);
console.log(m[1]);
console.log(m[2]);
console.log(m[3]);

// Test 5: regex.exec() no match returns null
var re3 = /xyz/;
console.log(re3.exec("hello"));

// Test 6: regex.source and regex.flags
var re4 = /abc/gi;
console.log(re4.source);
console.log(re4.flags);
