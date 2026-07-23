const proxy = new Proxy(Array.prototype, {});
proxy.push = function () { return 106; };
console.log([1].push(2));
