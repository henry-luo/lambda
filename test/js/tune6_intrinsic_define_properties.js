Object.defineProperties(Array.prototype, {
    push: {
        value: function () { return 102; },
        writable: true,
        configurable: true
    }
});
console.log([1].push(2));
