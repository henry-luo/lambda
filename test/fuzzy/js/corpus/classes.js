// Edge case: class features
class Base {
  constructor(x) { this.x = x; }
  method() { return this.x; }
  static create(x) { return new Base(x); }
  get value() { return this.x; }
  set value(v) { this.x = v; }
}

class Derived extends Base {
  constructor(x, y) {
    super(x);
    this.y = y;
  }
  method() { return super.method() + this.y; }
}

var b = new Base(10);
b.method();
b.value;
b.value = 20;
b.value;
Base.create(30).method();

var d = new Derived(1, 2);
d.method();
d instanceof Base;
d instanceof Derived;

// toString / valueOf
class MyNum {
  constructor(n) { this.n = n; }
  valueOf() { return this.n; }
  toString() { return "MyNum(" + this.n + ")"; }
}
var mn = new MyNum(42);
mn + 1;
"" + mn;

// Computed method names
var method = "hello";
class Comp {
  [method]() { return "world"; }
}
new Comp().hello();

// Class expression
var Cls = class NamedExpr {
  who() { return "NamedExpr"; }
};
new Cls().who();

// Empty class
class Empty {}
new Empty();

// Extend null
try {
  class NullBase extends null {
    constructor() {}
  }
  new NullBase();
} catch(e) {}
