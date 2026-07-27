const url = require('url');
const parsed = url.parse('https://example.test/a?b=c#d');
const params = new url.URLSearchParams('a=1&a=2');
const legacy = url.parse('/legacy?key=value', true);

console.log(url.format(parsed));
console.log(url.resolve('https://example.test/a/', '../b'));
console.log(url.resolve('/foo/bar/baz', '../quux'));
console.log(url.resolve('foo/bar', '../../../baz'));
console.log(params.toString());
console.log(url === require('node:url'));
console.log(legacy.query.key);
console.log(Object.getPrototypeOf(legacy.query) === null);
console.log(new url.Url().href === null);
