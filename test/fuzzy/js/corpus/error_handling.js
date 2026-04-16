// Edge case: error handling patterns
try { null.x; } catch(e) { e instanceof TypeError; }
try { undefined(); } catch(e) { e instanceof TypeError; }
try { ({})[Symbol.iterator](); } catch(e) {}
try { new 42(); } catch(e) {}
try { eval("}{"); } catch(e) { e instanceof SyntaxError; }
try { decodeURIComponent("%"); } catch(e) {}

// Nested try/catch/finally
try {
  try {
    throw new Error("inner");
  } finally {
    var finalized = true;
  }
} catch(e) {
  e.message;
  finalized;
}

// Error subclasses
try { throw new TypeError("type"); } catch(e) { e instanceof Error; }
try { throw new RangeError("range"); } catch(e) { e instanceof Error; }
try { throw new ReferenceError("ref"); } catch(e) { e instanceof Error; }

// Throw non-Error
try { throw 42; } catch(e) { e; }
try { throw "string error"; } catch(e) { e; }
try { throw null; } catch(e) { e; }
try { throw undefined; } catch(e) { e; }
try { throw {message: "obj"}; } catch(e) { e.message; }

// Stack overflow (guarded)
function recurse(n) {
  if (n > 0) return recurse(n - 1);
  return 0;
}
try { recurse(100000); } catch(e) {}

// Custom error
function MyError(msg) { this.message = msg; this.name = "MyError"; }
MyError.prototype = Object.create(Error.prototype);
try { throw new MyError("custom"); } catch(e) { e.name + ": " + e.message; }
