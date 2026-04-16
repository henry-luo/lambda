// Edge case: closures, scoping, hoisting
function outer() {
  var x = 10;
  function inner() { return x; }
  x = 20;
  return inner();
}
outer();

// Closure over loop variable (var vs let)
var fns = [];
for (var i = 0; i < 5; i++) {
  fns.push(function() { return i; });
}
fns[0]();
fns[4]();

var fns2 = [];
for (let j = 0; j < 5; j++) {
  fns2.push(function() { return j; });
}
fns2[0]();
fns2[4]();

// Hoisting
foo();
function foo() { return 1; }

// TDZ
try { bar; } catch(e) {}
let bar = 42;

// IIFE
(function() { var secret = 42; return secret; })();

// Arrow this binding
var obj = {
  value: 10,
  getArrow: function() {
    return (() => this.value)();
  }
};
obj.getArrow();

// Nested closures
function nest(n) {
  if (n <= 0) return function() { return 0; };
  var f = nest(n - 1);
  return function() { return n + f(); };
}
nest(5)();
