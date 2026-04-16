// Stress: scope torture — shadowing, TDZ, hoisting edge cases, eval
// Tests scope chain resolution under adversarial conditions

// Deep variable shadowing
var x = 1;
(function() {
  var x = 2;
  (function() {
    var x = 3;
    (function() {
      var x = 4;
      (function() {
        var x = 5;
        x;
      })();
      x;
    })();
    x;
  })();
  x;
})();
x;

// let in blocks — same name, different scopes
var results = [];
{ let v = 1; results.push(v); }
{ let v = 2; results.push(v); }
{ let v = 3; results.push(v); }
results;

// Closure captures mutable var vs let
var varFns = [];
for (var i = 0; i < 5; i++) { varFns.push(function() { return i; }); }
var letFns = [];
for (let j = 0; j < 5; j++) { letFns.push(function() { return j; }); }
[varFns[0](), varFns[4](), letFns[0](), letFns[4]()];

// Function hoisting
hoisted();
function hoisted() { return 1; }

// TDZ — let/const before declaration
try { tdzVar; } catch(e) {}
let tdzVar = 42;

// Function expression name scope
var feName = (function named() {
  return typeof named;
})();
try { typeof named; } catch(e) {} // named not visible outside

// arguments vs parameter names
function argsTest(a, b) {
  arguments[0] = 99;
  return [a, b, arguments[0], arguments[1], arguments.length];
}
argsTest(1, 2);

// eval creating variables (sloppy mode)
var evalVar = 10;
try { eval("var evalVar = 20;"); } catch(e) {}
evalVar;

// catch variable scope
var catchOuter = "outer";
try { throw "inner"; } catch(catchOuter) { catchOuter; }
catchOuter; // should still be "outer"

// Block-scoped function declaration
var blockFnResult = [];
{
  function blockFn() { return 1; }
  blockFnResult.push(blockFn());
}
try { blockFnResult.push(blockFn()); } catch(e) { blockFnResult.push("error"); }

// Parameter default using earlier parameter
function defaults(a, b = a * 2, c = a + b) {
  return [a, b, c];
}
defaults(1);
defaults(1, 10);

// Rest parameter
function restTest(first, ...rest) {
  return [first, rest.length];
}
restTest(1, 2, 3, 4);

// Destructured params with defaults
function destruct({a, b = 10}, [c, d = 20]) {
  return [a, b, c, d];
}
try { destruct({a: 1}, [2]); } catch(e) {}

// Nested function declarations — hoisting rules
function outer() {
  var r = inner();
  function inner() { return 42; }
  return r;
}
outer();

// Variable shadowing across function/block
var shadow = "global";
function shadowTest() {
  var shadow = "function";
  {
    let shadow = "block";
    shadow;
  }
  return shadow;
}
shadowTest();
shadow;
