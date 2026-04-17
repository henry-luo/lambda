// Test negative lookahead — wrapper erases (?!Y) and adds PF_REJECT_MATCH filter

// --- Test 1: Trailing negative lookahead ---
var re1 = /foo(?!bar)/;
console.log("t1:" + re1.test("foobaz") + "," + re1.test("foobar"));

// --- Test 2: Leading negative lookahead ---
var re2 = /(?!un)\w+able/;
// "unable" starts with "un" so should be rejected, "stable" should match
console.log("t2:" + re2.test("stable") + "," + re2.test("unable"));

// --- Test 3: Negative lookahead with exec ---
var re3 = /\d+(?!px)/;
var m3 = re3.exec("10em 20px");
console.log("t3:" + (m3 ? m3[0] : "null"));

// --- Test 4: Negative lookahead preventing match ---
var re4 = /^(?!.*error).*$/;
console.log("t4:" + re4.test("all good") + "," + re4.test("has error"));
