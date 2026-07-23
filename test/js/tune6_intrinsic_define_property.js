Object.defineProperty(Array.prototype, "push", {
    value: function () { return 101; },
    writable: true,
    configurable: true
});
console.log([1].push(2));
