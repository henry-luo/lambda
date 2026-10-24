const own = { value: 7 };
console.log(own.value === 7);
Object.defineProperty(own, 'value', { value: 8, writable: false, enumerable: false });
console.log(own.value === 8 && Object.keys(own).length === 0);
let getterCalls = 0;
Object.defineProperty(own, 'value', { get() { getterCalls++; return 9; }, configurable: true });
console.log(own.value === 9 && own.value === 9 && getterCalls === 2);
const child = Object.create({ get value() { return this.marker; } });
child.marker = 11;
console.log(child.value === 11);
console.log(Reflect.get(child, 'value', { marker: 12 }) === 12);
let proxyCalls = 0;
const proxy = new Proxy({ value: 13 }, { get(target, key, receiver) {
    proxyCalls++; return Reflect.get(target, key, receiver);
} });
console.log(proxy.value === 13 && proxyCalls === 1);
const deleted = Object.create({ value: 14 });
deleted.value = 15;
delete deleted.value;
console.log(deleted.value === 14);
deleted.value = 16;
console.log(deleted.value === 16);
console.log('a\u{1f600}'.length === 3 && (17).toString() === '17');
function Box() {
    const before = this.value;
    this.value = 18;
    this.before = before;
}
Box.prototype.value = 19;
const box = new Box();
console.log(box.before === 19 && box.value === 18);
let keyCalls = 0;
const key = { [Symbol.toPrimitive]() { keyCalls++; return 'value'; } };
console.log(deleted[key] === 16 && keyCalls === 1);
const symbol = Symbol('value');
deleted[symbol] = 20;
console.log(deleted[symbol] === 20 && deleted.value === 16);
Object.freeze(deleted);
console.log(deleted.value === 16);
let live = 20;
Object.defineProperty(globalThis, 'namedReadLive', {
    get() { return ++live; }, configurable: true
});
console.log(globalThis.namedReadLive === 21 && globalThis.namedReadLive === 22);
delete globalThis.namedReadLive;
const unicode = { '\u00e9': 23, e: 24 };
console.log(unicode['\u00e9'] === 23 && unicode.e === 24);
const number = { value: -1.25 };
const saved = number.value;
number.value = -9.5;
for (let index = 0; index < 2000; index++) ({ value: index / 3 });
console.log(saved === -1.25 && number.value === -9.5);
console.log(Box.name === 'Box' && Box.prototype.constructor === Box);
const array = [25];
array.value = 26;
console.log(array.value === 26 && array[0] === 25 && array.length === 1);
Object.defineProperty(own, 'value', { value: 27, writable: true });
console.log(own.value === 27 && getterCalls === 2);
let thrown;
Object.defineProperty(own, 'value', { get() { throw own; } });
try { own.value; } catch (error) { thrown = error; }
console.log(thrown === own);
console.log(Reflect.get({ value: 31 }, 'value', { value: 0 }) === 31);
const charCodeAtDescriptor = Object.getOwnPropertyDescriptor(String.prototype, 'charCodeAt');
console.log('ABC'.charCodeAt(1) === 66);
String.prototype.charCodeAt = function (index) { 'use strict'; return this.length + index; };
console.log('abc'.charCodeAt(2) === 5);
let primitiveGetterCalls = 0, primitiveReceiver;
Object.defineProperty(String.prototype, 'charCodeAt', {
    get() { 'use strict'; primitiveGetterCalls++; primitiveReceiver = this;
        return function (index) { return index + 32; }; }, configurable: true
});
console.log('abc'.charCodeAt(2) === 34 && primitiveGetterCalls === 1 && primitiveReceiver === 'abc');
delete String.prototype.charCodeAt;
Object.prototype.charCodeAt = function () { return 33; };
console.log('abc'.charCodeAt(0) === 33);
delete Object.prototype.charCodeAt;
Object.defineProperty(String.prototype, 'charCodeAt', charCodeAtDescriptor);
String.prototype.namedReadTag = 36;
console.log(new String('abc').namedReadTag === 36 && 'ABC'.charCodeAt(1) === 66);
delete String.prototype.namedReadTag;
console.log('DONE');
