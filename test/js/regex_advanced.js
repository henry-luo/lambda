// RegExp constructor and advanced regex tests

// --- Test 1: RegExp constructor ---
var re1 = new RegExp("hello", "i");
console.log("t1:" + re1.test("HELLO world") + "," + re1.source + "," + re1.flags);

// --- Test 2: RegExp from string pattern ---
var pattern = "\\d{3}-\\d{4}";
var re2 = new RegExp(pattern);
console.log("t2:" + re2.test("555-1234") + "," + re2.test("abc-defg"));

// --- Test 3: exec with capture groups ---
var re3 = /(\w+)@(\w+)\.(\w+)/;
var m = re3.exec("user@example.com");
console.log("t3:" + m[0] + "," + m[1] + "," + m[2] + "," + m[3]);

// --- Test 4: String.match ---
var s = "The year 2024 and month 01";
var matches = s.match(/\d+/g);
console.log("t4:" + matches.join(","));

// --- Test 5: String.replace with regex ---
var s2 = "hello world hello";
console.log("t5:" + s2.replace(/hello/g, "hi"));

// --- Test 6: String.replace with function ---
var s3 = "abc123def456";
var result = s3.replace(/\d+/g, function(match) { return "[" + match + "]"; });
console.log("t6:" + result);

// --- Test 7: String.split with regex ---
var s4 = "one,  two;three   four";
var parts = s4.split(/[,;\s]+/);
console.log("t7:" + parts.join("|"));

// --- Test 8: String.search ---
var s5 = "Hello World";
console.log("t8:" + s5.search(/world/i) + "," + s5.search(/xyz/));

// --- Test 9: regex flags ---
var re9a = /abc/g;
var re9b = /abc/i;
var re9c = /abc/m;
console.log("t9:" + re9a.global + "," + re9b.ignoreCase + "," + re9c.multiline);

// --- Test 10: character class patterns ---
var re10 = /^[A-Za-z_]\w*$/;
console.log("t10:" + re10.test("validName") + "," + re10.test("_private") + "," + re10.test("123bad") + "," + re10.test("also-bad"));
