// Test: function constructor returning object replaces this
function Ctor() {
  this.a = 1;
  return {b: 2};
}
var r = new Ctor();
console.log(r.a);       // undefined
console.log(r.b);       // 2

// Test: function constructor returning primitive is ignored
function Ctor2() {
  this.a = 1;
  return 42;
}
var r2 = new Ctor2();
console.log(r2.a);      // 1

// Test: class constructor returning object replaces this
class Foo {
  constructor() {
    this.a = 1;
    return {b: 2};
  }
}
var f = new Foo();
console.log(f.a);       // undefined
console.log(f.b);       // 2

// Test: class constructor returning primitive is ignored
class Bar {
  constructor() {
    this.x = 10;
    return 42;
  }
}
var b = new Bar();
console.log(b.x);       // 10

// Test: constructor returning array
function ArrCtor() {
  this.a = 1;
  return [1, 2, 3];
}
var ar = new ArrCtor();
console.log(Array.isArray(ar));  // true
console.log(ar.length);          // 3

// Test: constructor returning function
function FuncCtor() {
  this.a = 1;
  return function() { return 42; };
}
var fc = new FuncCtor();
console.log(typeof fc);  // function
console.log(fc());        // 42

// Test: constructor returning null is ignored (null is not an object)
function NullCtor() {
  this.a = 1;
  return null;
}
var nc = new NullCtor();
console.log(nc.a);       // 1

// Test: constructor returning undefined is ignored
function UndefCtor() {
  this.a = 1;
  return undefined;
}
var uc = new UndefCtor();
console.log(uc.a);       // 1

// Test: no explicit return uses this
function NoCtor() {
  this.a = 99;
}
var nr = new NoCtor();
console.log(nr.a);       // 99
