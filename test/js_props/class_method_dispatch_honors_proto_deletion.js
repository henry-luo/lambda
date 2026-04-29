// Invariant: deletion on an inherited proto level falls through to
// the next level. Companion to the own-deletion case: ensures the
// in-loop class-method dispatch in js_prototype_lookup_ex also
// respects the deleted sentinel.

function Animal() {}
Animal.prototype.greet = function() { return "hello-animal"; };
function Dog() {}
Dog.prototype = Object.create(Animal.prototype);
var d = new Dog();
console.log(d.greet());
delete Animal.prototype.greet;
console.log(d.greet === undefined);
Object.prototype.greet = function() { return "hello-object"; };
console.log(d.greet());
delete Object.prototype.greet;
console.log("OK");
