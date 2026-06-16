// Regression: a FLOAT-inferred class field assigned an integer-valued expression
// (e.g. `x % 500`) must read back correctly through the §7 shaped-slot fast path.
// The fast path used to do a raw double load of the tagged-int bytes -> garbage
// (AWFY "bounce" computed 1321 instead of 1331). The read now goes through the
// type-guarded js_get_slot_f/js_get_slot_i, which coerces by the runtime slot type.
class Src {
  constructor() { this.s = 1000; }
  next() { this.s = this.s + 1; return this.s; }
}
class Pt {
  constructor(src) { this.a = src.next() % 500; this.b = src.next() % 500; }
  sum() { return this.a + this.b; }   // reads this.a/this.b via the §7 fast path
}
var p = new Pt(new Src());
console.log("a=" + p.a);          // o.field read site (expression lowering)
console.log("b=" + p.b);
console.log("sum=" + p.sum());    // this.field read site (in a class method)
