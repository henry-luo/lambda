// querystring module basic tests
import qs from 'querystring';

// qs.parse - basic
var obj = qs.parse('foo=bar&baz=qux');
console.log(obj.foo);
console.log(obj.baz);

// qs.parse - encoded values
var obj2 = qs.parse('name=hello%20world&key=a%26b');
console.log(obj2.name);
console.log(obj2.key);

// qs.parse - empty string
var obj3 = qs.parse('');
console.log(typeof obj3);

// qs.stringify - basic
console.log(qs.stringify({ foo: 'bar', baz: 'qux' }));

// qs.escape
console.log(qs.escape('hello world'));
console.log(qs.escape('a&b=c'));

// qs.unescape
console.log(qs.unescape('hello%20world'));
console.log(qs.unescape('a%26b%3Dc'));

// qs.decode is alias for parse
var obj4 = qs.decode('x=1&y=2');
console.log(obj4.x);
console.log(obj4.y);

// qs.encode is alias for stringify
console.log(qs.encode({ a: '1', b: '2' }));
