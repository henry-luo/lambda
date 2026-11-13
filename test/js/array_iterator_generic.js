// Array iterators retain the receiver and read its live length only on next().
for (const method of ['keys', 'values', 'entries']) {
    let reads = 0;
    const source = {0: 'a', get length() { reads++; return 2; }};
    const iterator = Array.prototype[method].call(source);
    console.log(method + '-deferred:' + (reads === 0));
    source[0] = 'b';
    const first = iterator.next().value;
    console.log(method + '-first:' + (method === 'keys' ? first === 0 :
        method === 'values' ? first === 'b' : first.join(':') === '0:b'));
    source[1] = 'c';
    const second = iterator.next().value;
    console.log(method + '-second:' + (method === 'keys' ? second === 1 :
        method === 'values' ? second === 'c' : second.join(':') === '1:c'));
    console.log(method + '-done:' + iterator.next().done);
    Object.defineProperty(source, 'length', {get() { throw 'after-done'; }});
    console.log(method + '-permanent:' + iterator.next().done);
}
let lengthReads = 0;
const throwing = {get length() { lengthReads++; throw 'length'; }};
const deferred = Array.prototype.values.call(throwing);
console.log('throw-deferred:' + (lengthReads === 0));
try { deferred.next(); } catch (error) { console.log('length-error:' + (error === 'length')); }
const poisonedIndex = {length: 2, get 0() { throw 'index'; }, 1: 'next'};
const indexIterator = Array.prototype.values.call(poisonedIndex);
try { indexIterator.next(); } catch (error) { console.log('index-error:' + (error === 'index')); }
console.log('index-advanced:' + (indexIterator.next().value === 'next'));
const live = {length: 1, 0: 'a'};
const liveIterator = Array.prototype.values.call(live);
liveIterator.next();
live.length = 2;
live[1] = 'b';
console.log('growth:' + (liveIterator.next().value === 'b'));
live.length = -1;
console.log('negative-length:' + liveIterator.next().done);
let trace = '';
const proxy = new Proxy({length: 1, 0: 'p'}, {get(target, key) {
    trace += String(key) + ';'; return target[key];
}});
const proxyIterator = Array.prototype.values.call(proxy);
console.log('proxy-deferred:' + (trace === ''));
console.log('proxy-value:' + (proxyIterator.next().value === 'p' && trace === 'length;0;'));
console.log('string:' + (Array.prototype.values.call('ab').next().value === 'a'));
console.log('number:' + Array.prototype.keys.call(42).next().done);
let nullErrors = 0;
for (const method of ['keys', 'values', 'entries']) {
    try { Array.prototype[method].call(null); } catch (error) { if (error instanceof TypeError) nullErrors++; }
}
console.log('null-errors:' + (nullErrors === 3));
function callable() {}
callable[Symbol.iterator] = function() { return ['callable'].values(); };
let collected = '';
for (const value of callable) collected += value;
console.log('callable:' + (collected === 'callable'));
