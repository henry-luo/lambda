Reflect.defineProperty(Array.prototype, "push", {
    value: function () { return 103; },
    writable: true,
    configurable: true
});
console.log([1].push(2));
