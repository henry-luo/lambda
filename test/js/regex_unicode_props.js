// Test Unicode property escapes expanded by Phase A regex wrapper
// NOTE: Using RegExp constructor because Tree-sitter parser doesn't support \p{} in regex literals

// --- Test 1: \p{XID_Start} matches Unicode identifier start characters ---
var re1 = new RegExp("^\\p{XID_Start}$", "u");
console.log("t1:" + re1.test("A") + "," + re1.test("é") + "," + re1.test("中") + "," + re1.test("5"));

// --- Test 2: \p{XID_Continue} matches Unicode identifier continue characters ---
var re2 = new RegExp("^\\p{XID_Continue}$", "u");
console.log("t2:" + re2.test("A") + "," + re2.test("5") + "," + re2.test("_") + "," + re2.test(" "));

// --- Test 3: \p{ASCII} matches ASCII characters ---
var re3 = new RegExp("^\\p{ASCII}+$", "u");
console.log("t3:" + re3.test("hello123") + "," + re3.test("café"));

// --- Test 4: \p{Script=Latin} maps to \p{Latin} ---
var re4 = new RegExp("^\\p{Script=Latin}+$", "u");
console.log("t4:" + re4.test("Hello") + "," + re4.test("日本語"));

// --- Test 5: \p{General_Category=Nd} maps to \p{Nd} (decimal digits) ---
var re5 = new RegExp("^\\p{General_Category=Nd}+$", "u");
console.log("t5:" + re5.test("12345") + "," + re5.test("abc"));

// --- Test 6: \p{LC} (cased letter) matches upper/lower/title case ---
var re6 = new RegExp("^\\p{LC}+$", "u");
console.log("t6:" + re6.test("Hello") + "," + re6.test("123"));

// --- Test 7: Mixed Unicode property escapes in a pattern ---
var re7 = new RegExp("^\\p{XID_Start}\\p{XID_Continue}*$", "u");
console.log("t7:" + re7.test("myVar123") + "," + re7.test("_test") + "," + re7.test("123abc"));

// --- Test 8: Negated Unicode property \P{ASCII} ---
var re8 = new RegExp("\\P{ASCII}", "u");
console.log("t8:" + re8.test("hello") + "," + re8.test("café"));
