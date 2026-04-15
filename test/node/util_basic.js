// util module basic tests
import util from 'util';

// util.format
console.log(util.format('hello %s', 'world'));
console.log(util.format('%d + %d = %d', 1, 2, 3));
console.log(util.format('value: %j', { a: 1 }));

// util.inspect
console.log(typeof util.inspect({ a: 1 }));

// util.isDeepStrictEqual
console.log(util.isDeepStrictEqual({ a: 1 }, { a: 1 }));
console.log(util.isDeepStrictEqual({ a: 1 }, { a: 2 }));
console.log(util.isDeepStrictEqual([1, 2, 3], [1, 2, 3]));
console.log(util.isDeepStrictEqual([1, 2], [1, 2, 3]));

// util.types
console.log(util.types.isDate(new Date()));
console.log(util.types.isDate({}));
console.log(util.types.isRegExp(/abc/));
console.log(util.types.isRegExp('abc'));
console.log(util.types.isArray([1, 2]));
console.log(util.types.isArray('hello'));
