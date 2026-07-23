Reflect.set(Array.prototype, "push", function () { return 104; });
console.log([1].push(2));
