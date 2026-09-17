var input = { toString: function () { return 'child'; } };
var base = { toString: function () { return 'https://example.com/a/'; } };
var scope = new URL('.', location);

console.log(new URL(input, base).href);
console.log(String(location) === location.href);
console.log(scope.href === new URL('.', location.href).href);
