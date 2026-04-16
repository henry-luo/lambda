// Stress: property storms — add/delete/redefine, hidden class transitions
// Tests shape/hidden-class system stability

// Add many properties dynamically
var obj = {};
for (var i = 0; i < 200; i++) {
  obj["prop" + i] = i;
}
Object.keys(obj).length;

// Add then delete alternating
var obj2 = {};
for (var i = 0; i < 200; i++) {
  obj2["p" + i] = i;
}
for (var i = 0; i < 200; i += 2) {
  delete obj2["p" + i];
}
Object.keys(obj2).length;

// Redefine property with different descriptors
var obj3 = {};
Object.defineProperty(obj3, "x", {value: 1, writable: true, enumerable: true, configurable: true});
obj3.x;
Object.defineProperty(obj3, "x", {value: 2, writable: false});
try { obj3.x = 99; } catch(e) {}
obj3.x;
Object.defineProperty(obj3, "x", {get() { return 42; }, configurable: true});
obj3.x;

// Freeze / seal / preventExtensions
var frozen = Object.freeze({a: 1, b: 2, c: {d: 3}});
try { frozen.a = 99; } catch(e) {}
try { frozen.newProp = 1; } catch(e) {}
try { delete frozen.a; } catch(e) {}
frozen.c.d = 99; // nested object is NOT frozen

var sealed = Object.seal({x: 1, y: 2});
sealed.x = 99; // OK — writable
try { sealed.z = 3; } catch(e) {} // fails — not extensible
try { delete sealed.x; } catch(e) {} // fails — not configurable

var noExtend = Object.preventExtensions({a: 1});
try { noExtend.b = 2; } catch(e) {}
noExtend.a = 99; // OK
delete noExtend.a; // OK

// Prototype property shadowing
function Base() { this.x = 1; }
Base.prototype.x = 0;
Base.prototype.y = 100;
var inst = new Base();
inst.x; // 1 (own)
inst.y; // 100 (prototype)
delete inst.x;
inst.x; // 0 (falls to prototype)
inst.y = 200;
Base.prototype.y; // still 100

// Megamorphic call site — many different shapes
function readX(o) { return o.x; }
readX({x: 1});
readX({x: 2, y: 1});
readX({x: 3, y: 1, z: 1});
readX({a: 1, x: 4});
readX({a: 1, b: 2, x: 5});
readX({a: 1, b: 2, c: 3, x: 6});
for (var i = 0; i < 30; i++) {
  var o = {x: i};
  for (var j = 0; j < i; j++) o["extra" + j] = j;
  readX(o);
}

// Computed property keys
var keys = ["a", "b", "c", "d", "e"];
var computed = {};
for (var k of keys) {
  computed[k] = k.charCodeAt(0);
}

// Symbol properties mixed with string properties
var sym1 = Symbol("s1");
var sym2 = Symbol("s2");
var mixed = {a: 1, [sym1]: 2, b: 3, [sym2]: 4};
Object.keys(mixed);
Object.getOwnPropertyNames(mixed);
Object.getOwnPropertySymbols(mixed);

// Non-standard: numeric string keys
var numObj = {};
numObj["0"] = "a";
numObj["1"] = "b";
numObj["2"] = "c";
numObj[0]; // should be same as ["0"]
Object.keys(numObj);

// Property enumeration order
var orderTest = {};
orderTest["2"] = "c";
orderTest["0"] = "a";
orderTest["b"] = "B";
orderTest["1"] = "b";
orderTest["a"] = "A";
Object.keys(orderTest); // numeric first, then insertion order
