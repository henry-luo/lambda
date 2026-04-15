// util extended tests — promisify, TextEncoder/TextDecoder, more types
var util = require('node:util');

// util.format with various specifiers
console.log(util.format('%s:%s', 'key', 'value'));
console.log(util.format('%d', 42));
console.log(util.format('%i', 3.7));
console.log(util.format('%f', 3.14));
console.log(util.format('%%'));
console.log(util.format('%j', [1, 2, 3]));

// util.format with extra args
console.log(util.format('a', 'b', 'c'));

// util.inspect
var obj = { name: 'test', value: 123 };
var inspected = util.inspect(obj);
console.log('inspect type:', typeof inspected);
console.log('inspect has name:', inspected.indexOf('name') >= 0);

// util.types - extended checks
console.log('isPromise:', util.types.isPromise(Promise.resolve(1)));
console.log('isPromise false:', util.types.isPromise({}));
console.log('isError:', util.types.isError(new Error('test')));
console.log('isError false:', util.types.isError('not error'));
console.log('isFunction:', util.types.isFunction(function() {}));
console.log('isFunction false:', util.types.isFunction(42));
console.log('isString:', util.types.isString('hello'));
console.log('isString false:', util.types.isString(42));
console.log('isNumber:', util.types.isNumber(42));
console.log('isNumber false:', util.types.isNumber('42'));
console.log('isBoolean:', util.types.isBoolean(true));
console.log('isNull:', util.types.isNull(null));
console.log('isUndefined:', util.types.isUndefined(undefined));
console.log('isObject:', util.types.isObject({}));
console.log('isPrimitive str:', util.types.isPrimitive('hello'));
console.log('isPrimitive obj:', util.types.isPrimitive({}));

// TextEncoder
var encoder = new util.TextEncoder();
var encoded = encoder.encode('hello');
console.log('TextEncoder len:', encoded.length);

// TextDecoder
var decoder = new util.TextDecoder();
var decoded = decoder.decode(encoded);
console.log('TextDecoder:', decoded);

// util.deprecate - returns a function
var fn = util.deprecate(function() { return 42; }, 'deprecated');
console.log('deprecate type:', typeof fn);
console.log('deprecate call:', fn());

// util.inherits
function Parent() {}
Parent.prototype.hello = function() { return 'hi'; };
function Child() {}
util.inherits(Child, Parent);
var child = new Child();
console.log('inherits:', child.hello());
