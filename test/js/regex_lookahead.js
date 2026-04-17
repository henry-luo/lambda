// Test positive lookahead — wrapper converts X(?=Y) to X(Y) with post-filter trim

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
