const params = new URLSearchParams('a=1&b=2&a=3');
const sharedIteratorPrototype = Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]()));
console.log(params[Symbol.iterator] === params.entries);
console.log(Array.isArray(params.entries()));
console.log(Object.prototype.toString.call(params.entries()));
console.log(Object.getPrototypeOf(Object.getPrototypeOf(params.entries())) === sharedIteratorPrototype);
console.log(JSON.stringify(Array.from(params)));
console.log(JSON.stringify(Array.from(params.keys())));
console.log(JSON.stringify(Array.from(params.values())));
let joined = '';
for (const [key, value] of params) joined += key + '=' + value + ';';
console.log(joined);

const live = new URLSearchParams('a=1&b=2');
const iterator = live.entries();
console.log(iterator[Symbol.iterator]() === iterator);
const first = iterator.next();
first.value[0] = 'changed';
console.log(live.get('a'));
live.set('b', 'updated');
if (typeof gc === 'function') gc();
console.log(JSON.stringify(iterator.next()));
console.log(JSON.stringify(iterator.next()));
// WebIDL iterators retain their index and observe appends even after returning done.
live.append('c', '3');
console.log(JSON.stringify(iterator.next()));

const deleted = new URLSearchParams('a=1&b=2&c=3');
const values = deleted.values();
console.log(values.next().value);
deleted.delete('b');
console.log(values.next().value);
console.log(values.next().done);

try { params.entries.call({}); } catch (error) { console.log(error instanceof TypeError); }
try { iterator.next.call({}); } catch (error) { console.log(error instanceof TypeError); }

// Intrinsic iteration identities survive replacement of the exposed globals.
const OriginalSymbol = Symbol;
const OriginalIterator = globalThis.Iterator;
globalThis.Symbol = {};
globalThis.Iterator = {};
const independent = new URLSearchParams('intrinsic=yes');
console.log(JSON.stringify(Array.from(independent)));
console.log(independent[OriginalSymbol.iterator] === independent.entries);
console.log(Object.getPrototypeOf(Object.getPrototypeOf(independent.entries())) === sharedIteratorPrototype);
globalThis.Symbol = OriginalSymbol;
globalThis.Iterator = OriginalIterator;

let retained = new URLSearchParams('retained=through-gc').entries();
if (typeof gc === 'function') gc();
console.log(JSON.stringify(retained.next()));
