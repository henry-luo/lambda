const url = new URL('/guide?q=1', 'https://example.com/base/');

console.log(url.toString());
console.log(String(url));
console.log(`${url}`);
console.log(Object.prototype.propertyIsEnumerable.call(url, 'toString'));
