// Stress: exception path torture — rapid throw/catch, finally, nested exceptions
// Tests exception handling doesn't leak memory or corrupt state

// Rapid throw/catch loop
var count = 0;
for (var i = 0; i < 500; i++) {
  try { throw i; } catch(e) { count += e; }
}
count;

// Finally overwrites return
function finallyReturn() {
  try { return 1; } finally { return 2; }
}
finallyReturn();

// Nested try with throw in finally
try {
  try {
    try { throw "a"; }
    finally { throw "b"; }
  }
  finally { throw "c"; }
}
catch(e) { e; }

// Exception types
try { null.x; } catch(e) { e instanceof TypeError; }
try { undeclaredVar123; } catch(e) { e instanceof ReferenceError; }
try { eval("}{"); } catch(e) { e instanceof SyntaxError; }
try { decodeURIComponent("%"); } catch(e) { e instanceof URIError; }
try { new Array(-1); } catch(e) { e instanceof RangeError; }

// Throw non-Error values
try { throw 42; } catch(e) { e === 42; }
try { throw "msg"; } catch(e) { e === "msg"; }
try { throw null; } catch(e) { e === null; }
try { throw undefined; } catch(e) { e === undefined; }
try { throw [1,2,3]; } catch(e) { e.length; }
try { throw {code: 42}; } catch(e) { e.code; }
try { throw function() {}; } catch(e) { typeof e; }

// Exception during forEach
var items = [1, 2, 3, 4, 5];
var result = [];
try {
  items.forEach(function(x) {
    if (x === 3) throw "stop";
    result.push(x * 2);
  });
} catch(e) {}
result;

// Exception in constructor
function BadCtor() { throw new Error("construct fail"); }
for (var i = 0; i < 50; i++) {
  try { new BadCtor(); } catch(e) {}
}

// Exception in getter
var getterThrow = {
  get val() { throw new Error("getter"); }
};
try { getterThrow.val; } catch(e) { e.message; }

// Exception in setter
var setterThrow = {
  set val(v) { throw new Error("setter"); }
};
try { setterThrow.val = 1; } catch(e) { e.message; }

// Exception in property access chain
var chain = {a: {b: {c: null}}};
try { chain.a.b.c.d.e; } catch(e) {}
try { chain.a.b.c(); } catch(e) {}
try { chain.x.y; } catch(e) {}

// Exception in map/filter/reduce
try { [1,2,3].map(function(x) { if (x === 2) throw "err"; return x; }); } catch(e) {}
try { [1,2,3].filter(function(x) { if (x === 2) throw "err"; return true; }); } catch(e) {}
try { [1,2,3].reduce(function(a, x) { if (x === 2) throw "err"; return a + x; }, 0); } catch(e) {}

// Exception with multiple catch variables (no conflict)
try { throw 1; } catch(e1) { e1; }
try { throw 2; } catch(e2) { e2; }
try { throw 3; } catch(e3) { e3; }

// Stack overflow caught
function deepRecurse(n) { return deepRecurse(n + 1); }
try { deepRecurse(0); } catch(e) {}

// Error properties
try {
  throw new Error("test");
} catch(e) {
  e.message;
  e.name;
  e.stack;
}
