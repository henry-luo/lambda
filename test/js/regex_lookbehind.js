// Test lookbehind stripping (Phase A) — RE2 doesn't support lookbehind,
// so (?<=...) and (?<!...) are stripped. The regex matches without the constraint.

// --- Test 1: Positive lookbehind stripped — matches without the behind constraint ---
var re1 = /(?<=@)\w+/;
console.log("t1:" + re1.test("user@domain") + "," + re1.test("hello"));

// --- Test 2: Negative lookbehind stripped — matches without the constraint ---
var re2 = /(?<!un)happy/;
console.log("t2:" + re2.test("happy") + "," + re2.test("unhappy"));

// --- Test 3: Lookbehind does not cause RE2 compile failure ---
var re3 = /(?<=\d{3})-\d{4}/;
console.log("t3:" + (re3 !== null));

// --- Test 4: Multiple lookbehinds stripped cleanly ---
var re4 = /(?<=\$)\d+(?<=\d)\.?\d*/;
console.log("t4:" + (re4 !== null) + "," + re4.test("$100.50"));
