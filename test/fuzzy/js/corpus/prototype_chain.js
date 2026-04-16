// Edge case: prototype chain manipulation
function Animal(name) { this.name = name; }
Animal.prototype.speak = function() { return this.name; };

function Dog(name) { Animal.call(this, name); }
Dog.prototype = Object.create(Animal.prototype);
Dog.prototype.constructor = Dog;
Dog.prototype.bark = function() { return "woof"; };

var d = new Dog("Rex");
d.speak();
d.bark();
d instanceof Dog;
d instanceof Animal;

// Property descriptors
var obj = {};
Object.defineProperty(obj, "x", { value: 42, writable: false, enumerable: true, configurable: false });
obj.x;
try { obj.x = 100; } catch(e) {}

// Getters/setters
var obj2 = {
  _val: 0,
  get val() { return this._val; },
  set val(v) { this._val = v * 2; }
};
obj2.val = 5;
obj2.val;

// Freeze, seal
var frozen = Object.freeze({a: 1, b: {c: 2}});
try { frozen.a = 99; } catch(e) {}
frozen.b.c = 99; // deep mutation still works

var sealed = Object.seal({x: 1});
try { sealed.y = 2; } catch(e) {}
sealed.x = 10;

// Null prototype
var bare = Object.create(null);
bare.key = "value";
bare.key;

// __proto__ chain
var p = {greet: function() { return "hi"; }};
var c = {__proto__: p};
c.greet();

// hasOwnProperty
var obj3 = {a: 1};
obj3.hasOwnProperty("a");
obj3.hasOwnProperty("toString");
