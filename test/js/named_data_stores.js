const object = { value: 1 };
const alias = object;
object.value = 2;
console.log(alias.value === 2);
object.value = 2.5;
console.log(object.value === 2.5);
object.value = 'text';
object.value = { nested: 3 };
console.log(object.value.nested === 3);
object.value = null;
object.value = 4;
console.log(object.value === 4);
let setterCalls = 0, assigned;
const accessor = { set value(value) { setterCalls++; assigned = value; } };
accessor.value = 5;
console.log(setterCalls === 1 && assigned === 5);
const inherited = Object.create(accessor);
inherited.value = 6;
console.log(setterCalls === 2 && assigned === 6 && !Object.hasOwn(inherited, 'value'));
Object.setPrototypeOf(object, accessor);
object.value = 7;
console.log(setterCalls === 2 && object.value === 7);
const fixed = {};
Object.defineProperty(fixed, 'value', { value: 8 });
fixed.value = 9;
let readonlyThrows = false;
try { (function () { 'use strict'; fixed.value = 9; })(); }
catch (error) { readonlyThrows = error instanceof TypeError; }
console.log(fixed.value === 8 && readonlyThrows);
Object.preventExtensions(object);
object.value = 10;
object.extra = 11;
console.log(object.value === 10 && !Object.hasOwn(object, 'extra'));
Object.freeze(object);
object.value = 12;
console.log(object.value === 10);
let proxyCalls = 0, proxyReceiver;
const proxyTarget = { value: 13 };
const proxy = new Proxy(proxyTarget, { set(target, key, value, receiver) {
    proxyCalls++; proxyReceiver = receiver; return Reflect.set(target, key, value, receiver);
} });
proxy.value = 14;
console.log(proxyCalls === 1 && proxyReceiver === proxy && proxyTarget.value === 14);
const descriptor = {};
Object.defineProperty(descriptor, 'value', { value: 15, writable: true });
descriptor.value = 16;
const attributes = Object.getOwnPropertyDescriptor(descriptor, 'value');
console.log(descriptor.value === 16 && !attributes.enumerable && !attributes.configurable);
const symbol = Symbol('value'), separate = { value: 17 };
separate[symbol] = 18;
separate.value = 19;
console.log(separate[symbol] === 18 && separate.value === 19);
const ownProto = {};
Object.defineProperty(ownProto, '__proto__', { value: 20, writable: true });
ownProto.__proto__ = 21;
console.log(ownProto.__proto__ === 21 && Object.getPrototypeOf(ownProto) === Object.prototype);
Object.prototype.namedStoreProbe = 22;
Object.prototype.namedStoreProbe = 23;
console.log(({}).namedStoreProbe === 23);
delete Object.prototype.namedStoreProbe;
var namedStoreGlobal = 24;
globalThis.namedStoreGlobal = 25;
console.log(namedStoreGlobal === 25);
function strings() {
    let value = '';
    value += 'left';
    const holder = { value: '' };
    holder.value = value;
    value += 'right';
    return holder.value === 'left' && value === 'leftright';
}
console.log(strings());
const array = [], holder = { value: [] };
holder.value = array;
array.push(26);
console.log(holder.value === array && holder.value[0] === 26);
class Private {
    #value = 27;
    set value(value) { this.#value = value; }
    get value() { return this.#value; }
}
const instance = new Private();
instance.value = 28;
console.log(instance.value === 28);
function Constructor() {
    this.before = Object.hasOwn(this, 'value');
    this.value = 29;
}
const constructed = new Constructor();
console.log(!constructed.before && constructed.value === 29);
console.log('DONE');
