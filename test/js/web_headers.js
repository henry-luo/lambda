'use strict';

function check(condition, message) {
    if (!condition) throw new Error(message);
}
function typeError(action) {
    try { action(); } catch (error) { return error instanceof TypeError; }
    return false;
}

check(Headers.length === 0 && Headers.name === 'Headers', 'constructor metadata');
check(typeError(() => Headers()), 'requires new');
for (const input of [null, true, 5, 'ab', ['ab'], [['a']], [['a', 'b', 'c']]]) {
    check(typeError(() => new Headers(input)), 'HeadersInit conversion');
}
const headers = new Headers([['Z', ' last\t'], ['A', 'first'], ['a', 'second']]);
check(headers instanceof Headers && Reflect.ownKeys(headers).length === 0, 'hidden native state');
check(Object.prototype.toString.call(headers) === '[object Headers]', 'native tag');
check(JSON.stringify([...headers]) === '[["a","first, second"],["z","last"]]', 'sorted combined entries');
check(headers.get('A') === 'first, second' && headers.has('Z'), 'case insensitive lookup');
check(headers.get('missing') === null && !headers.has('missing'), 'missing entry');
check(headers.append('a', 3) === undefined && headers.get('a') === 'first, second, 3', 'append conversion');
check(headers.set('A', false) === undefined && headers.get('a') === 'false', 'replace duplicates');
check(headers.delete('z') === undefined && !headers.has('z'), 'delete');
check(JSON.stringify([...headers.keys()]) === '["a"]', 'keys');
check(JSON.stringify([...headers.values()]) === '["false"]', 'values');
check(new Headers(headers).get('a') === 'false', 'copy constructor');
Object.freeze(headers);
headers.set('frozen', 'mutable internal list');
check(headers.get('frozen') === 'mutable internal list', 'freeze preserves internal mutability');
console.log('Headers construction and mutation passed');

for (const name of ['', 'a b', 'a:b', '\u00e9', '\u0100', '\u0000', 'a\r']) {
    check(typeError(() => new Headers([[name, 'value']])), 'invalid name');
    check(typeError(() => headers.get(name)), 'invalid lookup name');
}
for (const value of ['a\rb', 'a\nb', 'a\0b', '\u0100', '\ud800', Symbol('value')]) {
    check(typeError(() => headers.set('valid', value)), 'invalid value');
}
headers.set('latin', '\u00e9\u00ff');
check(headers.get('latin') === '\u00e9\u00ff', 'ByteString keeps Latin1');
headers.set('space', '\r\n\t value \t\n\r');
check(headers.get('space') === 'value', 'HTTP whitespace normalization');
headers.set('control', '\u0001\u000b\u007f');
check(headers.get('control') === '\u0001\u000b\u007f', 'other value bytes preserved');
check(typeError(() => headers.get(Symbol('name'))), 'implicit Symbol conversion');
const record = Object.create({inherited: 'skip'});
Object.defineProperty(record, 'hidden', {value: 'skip'});
record.visible = 7;
check(JSON.stringify([...new Headers(record)]) === '[["visible","7"]]', 'record own enumerable properties');
record[Symbol('key')] = 'value';
check(typeError(() => new Headers(record)), 'record Symbol key');

let order = [];
new Headers([[
    {toString() { order.push('name'); return 'a'; }},
    {toString() { order.push('value'); return 'b'; }}
], [{toString() { order.push('later'); return 'c'; }}, 'd']]);
check(order.join(',') === 'name,value,later', 'sequence conversion order');
order = [];
check(typeError(() => new Headers([['bad name', 'v'], [{toString() { order.push('later'); return 'c'; }}, 'd']])), 'name validation');
check(order.join(',') === 'later', 'convert entire sequence before validation');
const sentinel = new Error('conversion');
let closed = false;
const iterable = {
    [Symbol.iterator]() { return {
        next() { return {done: false, value: ['a', {toString() { throw sentinel; }}]}; },
        return() { closed = true; return {}; }
    }; }
};
try { new Headers(iterable); throw new Error('missing conversion exception'); }
catch (error) { check(error === sentinel, 'exception identity'); }
check(!closed, 'WebIDL sequence conversion leaves iterator open on error');
const reentrant = new Headers();
reentrant.append({toString() { reentrant.append('inner', 'retained'); return 'outer'; }}, 'value');
check(reentrant.get('inner') === 'retained', 'append preserves coercion mutation');
check(reentrant.get({toString() { reentrant.set('outer', 'changed'); return 'outer'; }}) === 'changed', 'get observes coercion mutation');
console.log('Headers validation and conversion passed');

const cookies = new Headers([['set-cookie', 'a=1'], ['Set-Cookie', 'b=2'], ['x', 'one'], ['X', 'two']]);
check(JSON.stringify(cookies.getSetCookie()) === '["a=1","b=2"]', 'separate cookies');
check(cookies.get('set-cookie') === 'a=1, b=2', 'combined cookie lookup');
check(JSON.stringify([...cookies]) === '[["set-cookie","a=1"],["set-cookie","b=2"],["x","one, two"]]', 'cookie iteration');
cookies.set('Set-Cookie', 'c=3');
check(JSON.stringify(cookies.getSetCookie()) === '["c=3"]', 'cookie replacement');
cookies.delete('set-cookie');
check(cookies.getSetCookie().length === 0, 'cookie deletion');

const live = new Headers([['b', '2'], ['d', '4']]);
const iterator = live.entries();
check(Object.prototype.toString.call(iterator) === '[object Headers Iterator]', 'iterator tag');
check(iterator[Symbol.iterator]() === iterator, 'iterator identity');
check(iterator.next().value[0] === 'b', 'iterator first');
live.append('c', '3');
check(iterator.next().value[0] === 'c', 'iterator sees insertion');
live.delete('d');
check(iterator.next().done, 'iterator sees deletion');
live.append('z', '26');
check(iterator.next().value[0] === 'z', 'iterator resumes after live insertion');
const visited = [];
const callbackThis = {};
live.forEach(function(value, key, source) {
    check(this === callbackThis && source === live, 'forEach callback arguments');
    visited.push(key + ':' + value);
    if (key === 'b') live.set('c', 'changed');
    if (key === 'c') live.delete('z');
}, callbackThis);
check(visited.join(',') === 'b:2,c:changed', 'forEach visits live list');
console.log('Headers cookies and live iteration passed');

const constructorDescriptor = Object.getOwnPropertyDescriptor(Headers, 'prototype');
check(!constructorDescriptor.writable && !constructorDescriptor.enumerable && !constructorDescriptor.configurable, 'constructor prototype descriptor');
check(Headers.prototype[Symbol.iterator] === Headers.prototype.entries, 'iterator method alias');
for (const [name, length] of [['append', 2], ['set', 2], ['delete', 1], ['get', 1], ['has', 1], ['getSetCookie', 0], ['entries', 0], ['keys', 0], ['values', 0], ['forEach', 1]]) {
    const descriptor = Object.getOwnPropertyDescriptor(Headers.prototype, name);
    const method = descriptor.value;
    check(method.name === name && method.length === length, 'method metadata');
    check(descriptor.writable && descriptor.enumerable && descriptor.configurable, 'method descriptor');
    check(!('prototype' in method) && typeError(() => new method()), 'method is not constructable');
    for (const fake of [{}, Object.create(Headers.prototype), new Proxy(headers, {}), null]) {
        check(typeError(() => method.call(fake)), 'receiver brand');
    }
}
check(typeError(() => iterator.next.call({})), 'iterator brand');
class Derived extends Headers {}
const derived = new Derived({x: 'subclass'});
check(derived instanceof Derived && derived.get('x') === 'subclass', 'subclass');
function Alternate() {}
const alternate = Reflect.construct(Headers, [{x: 'alternate'}], new Proxy(Alternate, {}));
check(Object.getPrototypeOf(alternate) === Alternate.prototype, 'proxy new target');
check(Headers.prototype.get.call(alternate, 'x') === 'alternate', 'alternate native brand');
Object.setPrototypeOf(derived, null);
check(Headers.prototype.get.call(derived, 'x') === 'subclass', 'brand survives prototype change');
console.log('Headers reflection and brands passed');
