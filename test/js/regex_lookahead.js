// Test positive lookahead wrapper behavior

// --- Test 1: Trailing positive lookahead (?=...) ---
var re1 = /foo(?=bar)/;
var m1 = re1.exec("foobar");
console.log("t1:" + (m1 !== null) + "," + (m1 ? m1[0] : "null"));

// --- Test 2: Positive lookahead should NOT consume the lookahead content ---
var s = "foobar";
var re2 = /foo(?=bar)/;
var m2 = re2.exec(s);
// Match should be "foo" only, not "foobar"
console.log("t2:" + (m2 ? m2[0] : "null") + "," + (m2 ? m2[0].length : -1));

// --- Test 3: Positive lookahead with capture group ---
var re3 = /(\w+)(?=\s)/;
var m3 = re3.exec("hello world");
console.log("t3:" + (m3 ? m3[0] : "null") + "," + (m3 ? m3[1] : "null"));

// --- Test 4: Positive lookahead non-match ---
var re4 = /foo(?=baz)/;
console.log("t4:" + re4.test("foobar"));

// --- Test 5: Multiple lookaheads in pattern ---
var re5 = /\d+(?=px)/g;
var s5 = "10px 20em 30px";
var matches = s5.match(re5);
console.log("t5:" + (matches ? matches.join(",") : "null"));

// --- Test 6: Lookahead with alternation ---
var re6 = /\w+(?=\.|$)/;
var m6 = re6.exec("hello.world");
console.log("t6:" + (m6 ? m6[0] : "null"));

// --- Test 7: Middle positive lookahead should be zero-width ---
var re7 = /a(?=b)b/;
var m7 = re7.exec("ab");
console.log("t7:" + (m7 ? m7[0] : "null"));

// --- Test 8: Leading positive lookahead should be zero-width ---
var re8 = /(?=a)a/;
var m8 = re8.exec("a");
console.log("t8:" + (m8 ? m8[0] : "null"));

// --- Test 9: Marked heading shape keeps the full match span ---
var re9 = /^ {0,3}(#{1,6})(?=\s|$)(.*)(?:\n+|$)/;
var m9 = re9.exec("# H1");
console.log("t9:" + (m9 ? m9[0] : "null") + "," + (m9 ? m9[0].length : -1) + "," + (m9 ? m9[1] : "null") + "," + (m9 ? m9[2] : "null"));

// --- Test 10: Lookahead branch failure must allow alternation fallback ---
var re10 = /^(`+|[^`])(?:(?= {2,}\n)|[\s\S]*?(?:(?=[\\<!\[`*_]|\b_|$)|[^ ](?= {2,}\n)))/;
var m10 = re10.exec("hello world");
console.log("t10:" + (m10 ? m10[0] : "null") + "," + (m10 ? m10[1] : "null"));
