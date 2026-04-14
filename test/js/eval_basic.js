// eval() basic tests

// --- Test 1: eval number expression ---
console.log("t1:" + eval("1 + 2"));

// --- Test 2: eval string literal ---
console.log("t2:" + eval("'hello'"));

// --- Test 3: eval variable reference ---
var x = 42;
console.log("t3:" + eval("x"));

// --- Test 4: eval complex expression ---
console.log("t4:" + eval("3 * 4 + 5"));

// --- Test 5: eval boolean ---
console.log("t5:" + eval("true"));

// --- Test 6: eval regexp literal ---
var re = eval("/hello/i");
console.log("t6:" + typeof(re) + "," + re.source + "," + re.test("HELLO"));

// --- Test 7: eval regexp with flags ---
var re2 = eval("/abc/gi");
console.log("t7:" + re2.flags);

// --- Test 8: eval non-string returns argument ---
console.log("t8:" + eval(42));
console.log("t8b:" + eval(true));

// --- Test 9: eval with template ---
var name = "world";
console.log("t9:" + eval("'hello ' + name"));

// --- Test 10: eval array expression ---
var result = eval("[1,2,3]");
console.log("t10:" + result.join(","));
