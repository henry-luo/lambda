'use strict';

function shadowedTypedArray(Uint8Array) {
    const values = new Uint8Array(2);
    values[0] = 7;
    return values[0];
}

function FakeTypedArray(length) {
    return { 0: 1, length: length };
}

console.log(shadowedTypedArray(FakeTypedArray));
console.log(shadowedTypedArray(globalThis.Uint8Array));

function shadowedTypedArrayCoercion(Uint8Array) {
    const values = new Uint8Array(1);
    return (values[0] ? 'truthy' : 'falsey') + ':' + (values[0] + 1);
}

function FakeStringTypedArray() {
    return { 0: '0' };
}

console.log(shadowedTypedArrayCoercion(FakeStringTypedArray));

function shadowedArray(Array) {
    const values = new Array(2);
    values[0] = 5;
    return values[0];
}

function FakeArray(length) {
    return { 0: 2, length: length };
}

console.log(shadowedArray(FakeArray));
console.log(shadowedArray(globalThis.Array));

let observedKey = '';
const proxy = new Proxy({}, {
    set: function (target, key, value, receiver) {
        observedKey = typeof key + ':' + key;
        return Reflect.set(target, key, value, receiver);
    }
});
proxy[3] = 9;
console.log(observedKey + ':' + proxy[3]);

const rejectingProxy = new Proxy({}, {
    set: function () { return false; }
});
try {
    rejectingProxy[4] = 10;
    console.log('missing strict proxy error');
} catch (error) {
    console.log(error.name);
}

const frozen = Object.freeze([1]);
try {
    frozen[0] = 2;
    console.log('missing frozen error');
} catch (error) {
    console.log(error.name + ':' + frozen[0]);
}

const partial = new Array(20000);
partial.fill(1, 100, 19900);
console.log((0 in partial) + ':' + (100 in partial) + ':' +
    (19999 in partial) + ':' + partial[100] + ':' + partial[19899]);

function NewTarget() {}
NewTarget.prototype = { marker: 23 };
const reflected = Reflect.construct(Array, [3], NewTarget);
console.log(Object.getPrototypeOf(reflected).marker + ':' + reflected.length);

const fractionalArray = [10, 20];
fractionalArray[1.5] = 7;
const fractionalTyped = new Uint8Array([4, 5]);
fractionalTyped[1.5] = 9;
console.log(fractionalArray[1.5] + ':' + fractionalArray[1] + ':' +
    String(fractionalTyped[1.5]) + ':' + fractionalTyped[1]);

const accessorArray = [];
accessorArray[1] = 3;
let accessorWrite;
Object.defineProperties(accessorArray, {
    '1': {
        set: function (value) { accessorWrite = value; },
        enumerable: true,
        configurable: true
    }
});
accessorArray[1] = 11;
console.log(accessorWrite + ':' + String(accessorArray[1]));

let superWriteCount = 0;
class NullSuperWrite {
    static run() {
        super[0] = superWriteCount += 1;
    }
}
Object.setPrototypeOf(NullSuperWrite, null);
try {
    NullSuperWrite.run();
    console.log('missing super error');
} catch (error) {
    console.log(error.name + ':' + superWriteCount);
}
