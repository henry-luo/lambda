const querystring = require('querystring');
const parsed = querystring.parse('a=1&a=2&empty');

console.log(require('node:querystring') === querystring);
console.log(querystring.stringify({ a: 'hello world', b: ['x', 'y'] }));
console.log(parsed.a[0] + ',' + parsed.a[1] + ',' + parsed.empty);
console.log(querystring.escape(12));
console.log(querystring.unescape('a%20b'));
console.log(querystring.unescapeBuffer('a+b', true).toString());
console.log(querystring.parse('a=a', null, null, {
  decodeURIComponent: (text) => text + text,
}).aa);
console.log(querystring.stringify({ aa: 'aa' }, null, null, {
  encodeURIComponent: (text) => text[0],
}));
console.log(Object.getPrototypeOf(parsed) === null);
