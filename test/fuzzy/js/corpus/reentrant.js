// Stress: re-entrancy — valueOf/toString/toPrimitive during operations
// Tests that the engine handles callbacks during coercion safely

// valueOf called during arithmetic
var calls = 0;
var tricky = { valueOf() { calls++; return calls; } };
tricky + tricky;       // valueOf called twice
tricky * tricky;
tricky - tricky;
calls;

// toString called during string coercion
var log = [];
var a = { toString() { log.push("a"); return "A"; } };
var b = { toString() { log.push("b"); return "B"; } };
a + b;
"" + a + b;
`${a}${b}`;
log;

// valueOf that returns object (falls through to toString)
var complex = {
  valueOf() { return {}; },  // not primitive, so toString is called next
  toString() { return "fallback"; }
};
try { complex + 1; } catch(e) {}
"" + complex;

// valueOf that throws
var exploder = { valueOf() { throw new Error("boom"); } };
try { exploder + 1; } catch(e) { e.message; }
try { exploder < 2; } catch(e) { e.message; }
try { +exploder; } catch(e) { e.message; }
try { exploder == 0; } catch(e) { e.message; }

// toPrimitive
var tpObj = {
  [Symbol.toPrimitive](hint) {
    if (hint === "number") return 42;
    if (hint === "string") return "hello";
    return true;
  }
};
+tpObj;
`${tpObj}`;
tpObj + "";

// Side effects in valueOf during comparison
var sideEffects = 0;
var cmpObj = { valueOf() { sideEffects++; return sideEffects; } };
[cmpObj, cmpObj, cmpObj].sort(function(a, b) { return a - b; });
sideEffects;

// valueOf that modifies the object itself
var selfMod = {
  _n: 0,
  valueOf() { this._n++; this["dyn" + this._n] = true; return this._n; }
};
selfMod + selfMod + selfMod;
Object.keys(selfMod);

// Comparator that mutates array during sort
var arr = [5, 3, 8, 1, 9, 2, 7, 4, 6];
var sortCalls = 0;
try {
  arr.sort(function(a, b) {
    sortCalls++;
    if (sortCalls < 5 && arr.length < 15) arr.push(0);
    return a - b;
  });
} catch(e) {}

// Getter that creates new properties
var growing = {
  _count: 0,
  get val() {
    this._count++;
    this["added" + this._count] = this._count;
    return this._count;
  }
};
growing.val;
growing.val;
growing.val;
Object.keys(growing).length;

// toString during property access
var keyObj = { toString() { return "x"; } };
var target = {x: 42};
target[keyObj];

// valueOf during array index
var idxObj = { valueOf() { return 1; } };
var arr2 = [10, 20, 30];
arr2[idxObj];
