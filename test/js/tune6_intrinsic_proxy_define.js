const proxy = new Proxy(Array.prototype, {});
Object.defineProperty(proxy, "push", {
    value: function () { return 105; },
    writable: true,
    configurable: true
});
console.log([1].push(2));
