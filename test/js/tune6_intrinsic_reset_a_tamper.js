Array.prototype.push = function () { return 107; };
Array.prototype[Symbol.iterator] = function () {
    return {next() { return {done: true}; }};
};
console.log([1].push(2) + ":" + [...[3, 4]].length);
