// try/catch/finally edge cases and error types

// --- Test 1: finally always runs ---
function test1() {
  try {
    return "try";
  } finally {
    console.log("t1_finally");
  }
}
console.log("t1:" + test1());

// --- Test 2: finally with throw ---
function test2() {
  try {
    throw new Error("oops");
  } catch(e) {
    return "caught:" + e.message;
  } finally {
    console.log("t2_finally");
  }
}
console.log("t2:" + test2());

// --- Test 3: nested try/catch ---
function test3() {
  try {
    try {
      throw new Error("inner");
    } catch(e) {
      throw new Error("wrapped:" + e.message);
    }
  } catch(e) {
    return e.message;
  }
}
console.log("t3:" + test3());

// --- Test 4: catch re-throw ---
function test4() {
  try {
    try {
      throw new TypeError("bad type");
    } catch(e) {
      if (e instanceof TypeError) throw e;
      return "wrong error";
    }
  } catch(e) {
    return e.constructor.name + ":" + e.message;
  }
}
console.log("t4:" + test4());

// --- Test 5: Error types ---
function testError(fn) {
  try { fn(); return "no_error"; }
  catch(e) { return e.constructor.name; }
}
console.log("t5a:" + testError(function() { throw new TypeError("t"); }));
console.log("t5b:" + testError(function() { throw new RangeError("r"); }));
console.log("t5c:" + testError(function() { throw new SyntaxError("s"); }));
console.log("t5d:" + testError(function() { throw new ReferenceError("r"); }));
console.log("t5e:" + testError(function() { throw new URIError("u"); }));

// --- Test 6: throw non-Error ---
function test6() {
  try {
    throw "string error";
  } catch(e) {
    return typeof(e) + ":" + e;
  }
}
console.log("t6:" + test6());

// --- Test 7: throw number ---
function test7() {
  try { throw 42; }
  catch(e) { return typeof(e) + ":" + e; }
}
console.log("t7:" + test7());

// --- Test 8: Error properties ---
var err = new Error("test message");
console.log("t8:" + err.message + "," + err.name);

// --- Test 9: custom error properties ---
function test9() {
  try {
    var e = new Error("fail");
    e.code = 404;
    throw e;
  } catch(err) {
    return err.message + "," + err.code;
  }
}
console.log("t9:" + test9());

// --- Test 10: try/catch in loop ---
var results = [];
for (var i = 0; i < 5; i++) {
  try {
    if (i % 2 === 0) throw new Error("even");
    results.push("ok" + i);
  } catch(e) {
    results.push("err" + i);
  }
}
console.log("t10:" + results.join(","));
