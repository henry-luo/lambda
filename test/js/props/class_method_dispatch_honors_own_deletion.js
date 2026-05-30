// Invariant: deletion on a built-in prototype must allow proto chain
// fallthrough to Object.prototype's generic method. Regression: stack
// overflow when Boolean.prototype.toString invoked itself recursively
// because class-method dispatch ignored the deleted sentinel.

delete Boolean.prototype.toString;
console.log(Boolean.prototype.toString === Object.prototype.toString);
console.log(Boolean.prototype.toString());
console.log("OK");
