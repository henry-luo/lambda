// LR09-30: capture groups have no ceiling. The old JS_REGEX_MAX_GROUPS (256)
// truncated silently, so a 300-group pattern reported 255 groups with the last
// undefined, and the backtracking matcher refused patterns past 255 outright.
// Every expectation here is Node's answer.

function repeat(s, n) { var out = ""; for (var i = 0; i < n; i++) out += s; return out; }

// --- Test 1: match reports every group, well past the old 256 ceiling ---
var n = 300;
var re1 = new RegExp(repeat("(a)", n));
var m1 = repeat("a", n).match(re1);
console.log("t1:" + (m1 ? m1.length - 1 : -1) + "," + (m1 ? m1[n] : "none"));

// --- Test 2: a group index above the old cap is not undefined ---
console.log("t2:" + (m1 ? m1[257] : "none") + "," + (m1 ? m1[256] : "none"));

// --- Test 3: exec agrees with match ---
var ex = re1.exec(repeat("a", n));
console.log("t3:" + (ex ? ex.length - 1 : -1) + "," + (ex ? ex[n] : "none"));

// --- Test 4: high-numbered $n replacement ---
// $300 parses as $30 then a literal "0", exactly as it does in Node.
console.log("t4:" + repeat("a", n).replace(re1, "[$300]") + "," +
            repeat("a", n).replace(re1, "[$257]"));

// --- Test 5: lookahead routes through the backtracking matcher, which used to
// return "no match" above 255 groups rather than falling back ---
var half = repeat("(a)", 150);
var re2 = new RegExp("(?=" + half + ")" + half);
var m2 = repeat("a", 150).match(re2);
console.log("t5:" + (m2 ? m2.length - 1 : -1) + "," + (m2 ? m2[300] : "none"));

// --- Test 6: the boundary the backtracking matcher used to refuse ---
var b = repeat("(a)", 128);
var re3 = new RegExp("(?=" + b + ")" + b);
console.log("t6:" + re3.test(repeat("a", 128)));
