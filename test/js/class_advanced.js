// Class getters, setters, and inheritance tests

// --- Test 1: basic class getter ---
class Rect {
  constructor(w, h) { this._w = w; this._h = h; }
  get area() { return this._w * this._h; }
  get perimeter() { return 2 * (this._w + this._h); }
}
var r = new Rect(3, 4);
console.log("t1:" + r.area + "," + r.perimeter);

// --- Test 2: class setter ---
class Temperature {
  constructor(c) { this._celsius = c; }
  get fahrenheit() { return this._celsius * 9 / 5 + 32; }
  set fahrenheit(f) { this._celsius = (f - 32) * 5 / 9; }
  get celsius() { return this._celsius; }
}
var t = new Temperature(0);
console.log("t2:" + t.fahrenheit);
t.fahrenheit = 212;
console.log("t2b:" + t.celsius);

// --- Test 3: inheritance with super ---
class Shape {
  constructor(name) { this.name = name; }
  area() { return 0; }
  describe() { return this.name + ":" + this.area(); }
}
class Circle extends Shape {
  constructor(r) { super("circle"); this.r = r; }
  area() { return Math.round(Math.PI * this.r * this.r); }
}
class Square extends Shape {
  constructor(s) { super("square"); this.s = s; }
  area() { return this.s * this.s; }
}
console.log("t3:" + new Circle(5).describe() + "," + new Square(4).describe());

// --- Test 4: super method call ---
class Base {
  greet() { return "hello"; }
}
class Child extends Base {
  greet() { return super.greet() + " world"; }
}
console.log("t4:" + new Child().greet());

// --- Test 5: static methods and fields ---
class Counter {
  static count = 0;
  static increment() { Counter.count++; return Counter.count; }
  static reset() { Counter.count = 0; }
}
Counter.increment();
Counter.increment();
Counter.increment();
console.log("t5:" + Counter.count);
Counter.reset();
console.log("t5b:" + Counter.count);

// --- Test 6: multi-level inheritance ---
class A { who() { return "A"; } }
class B extends A { who() { return super.who() + "B"; } }
class C extends B { who() { return super.who() + "C"; } }
console.log("t6:" + new C().who());

// --- Test 7: instanceof chain ---
class Animal {}
class Dog extends Animal {}
class Puppy extends Dog {}
var p = new Puppy();
console.log("t7:" + (p instanceof Puppy) + "," + (p instanceof Dog) + "," + (p instanceof Animal) + "," + (p instanceof Object));

// --- Test 8: getter in object literal ---
var obj = {
  _val: 0,
  get val() { return this._val; },
  set val(v) { this._val = v * 2; }
};
obj.val = 5;
console.log("t8:" + obj.val);
obj.val = 10;
console.log("t8b:" + obj.val);

// --- Test 9: constructor return ---
class Wrapper {
  constructor(v) {
    this.value = v;
    this.type = typeof(v);
  }
  toString() { return "[" + this.type + ":" + this.value + "]"; }
}
console.log("t9:" + new Wrapper(42).toString() + "," + new Wrapper("hi").toString());

// --- Test 10: method chaining ---
class Builder {
  constructor() { this.parts = []; }
  add(p) { this.parts.push(p); return this; }
  build() { return this.parts.join("-"); }
}
console.log("t10:" + new Builder().add("a").add("b").add("c").build());
