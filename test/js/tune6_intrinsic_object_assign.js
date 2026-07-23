Object.assign(Array.prototype, {
    map: function () { return ["assign"]; }
});
console.log([1, 2].map(x => x + 1).join(","));
