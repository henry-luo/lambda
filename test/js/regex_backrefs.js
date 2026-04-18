// Test backreference support — wrapper replaces \N with (.+?) and PF_GROUP_EQUALITY

// --- Test 1: Simple backreference \1 ---
var re1 = /(\w+)\s+\1/;
console.log("t1:" + re1.test("hello hello") + "," + re1.test("hello world"));

// --- Test 2: Backreference in HTML tag matching ---
var re2 = /<(\w+)>.*<\/\1>/;
console.log("t2:" + re2.test("<div>content</div>") + "," + re2.test("<div>content</span>"));

// --- Test 3: Backreference with exec ---
var re3 = /(['"])(.*?)\1/;
var m3 = re3.exec("she said 'hello world' today");
console.log("t3:" + (m3 ? m3[0] : "null") + "," + (m3 ? m3[1] : "null") + "," + (m3 ? m3[2] : "null"));

// --- Test 4: Multiple backreferences ---
var re4 = /(\w)(\w)\2\1/;
console.log("t4:" + re4.test("abba") + "," + re4.test("abcd"));
