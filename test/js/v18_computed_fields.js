// Phase 5: Computed class fields
// Phase 10C: Class static blocks

// 1. Computed instance field
var k = "x";
class A { [k] = 42; }
console.log(new A().x);

// 2. Computed static field
class B { static ["y"] = 99; }
console.log(B.y);

// 3. Computed field with expression key
class C { ["a" + "b"] = 100; }
console.log(new C().ab);

// 4. Mixed computed and regular fields
class D {
  x = 1;
  ["y"] = 2;
  z = 3;
}
var d = new D();
console.log(d.x, d.y, d.z);

// 5. Static block basic
class E {
  static x;
  static {
    E.x = 10;
    E.y = 20;
  }
}
console.log(E.x, E.y);

// 6. Static block with computation
class F {
  static values = [];
  static {
    for (var i = 0; i < 3; i++) {
      F.values.push(i * 10);
    }
  }
}
console.log(F.values.join(","));

// 7. Multiple static blocks
class G {
  static a = 1;
  static { G.a += 10; }
  static b = 2;
  static { G.b += 20; }
}
console.log(G.a, G.b);

// 8. Computed field with Symbol-like key (variable)
var sym = "secret";
class H {
  [sym] = "hidden";
  getSecret() { return this[sym]; }
}
console.log(new H().getSecret());

// 9. Computed static field + static block interaction
class I {
  static ["computed"] = 5;
  static {
    I.computed *= 2;
  }
}
console.log(I.computed);

// 10. Inheritance with computed fields
class Base {
  ["base_field"] = "from_base";
}
class Child extends Base {
  ["child_field"] = "from_child";
}
var child = new Child();
console.log(child.base_field, child.child_field);
