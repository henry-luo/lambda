'use strict';

function check(condition, message) {
    if (!condition) throw new Error(message);
}
function typeError(action) {
    try { action(); } catch (error) { return error instanceof TypeError; }
    return false;
}

for (const Strategy of [ByteLengthQueuingStrategy, CountQueuingStrategy]) {
    check(Strategy.length === 1, 'constructor arity');
    check(typeError(() => Strategy({highWaterMark: 1})), 'requires new');
    for (const init of [undefined, null, true, 5, {}, {highWaterMark: undefined}]) {
        check(typeError(() => new Strategy(init)), 'required dictionary member');
    }
    for (const input of [-Infinity, -5, -0, 0, Infinity, NaN]) {
        check(Object.is(new Strategy({highWaterMark: input}).highWaterMark, input), 'unrestricted double');
    }
    for (const pair of [[null, 0], [false, 0], [true, 1], ['3.5', 3.5], ['foo', NaN]]) {
        check(Object.is(new Strategy({highWaterMark: pair[0]}).highWaterMark, pair[1]), 'numeric conversion');
    }
    const sentinel = new Error('conversion');
    try {
        new Strategy({get highWaterMark() { throw sentinel; }});
        throw new Error('missing getter exception');
    } catch (error) { check(error === sentinel, 'getter exception identity'); }
    try {
        new Strategy({highWaterMark: {valueOf() { throw sentinel; }}});
        throw new Error('missing conversion exception');
    } catch (error) { check(error === sentinel, 'conversion exception identity'); }
    check(typeError(() => new Strategy({highWaterMark: Symbol('hwm')})), 'symbol conversion');

    const strategy = new Strategy({highWaterMark: 7});
    check(strategy instanceof Strategy, 'native prototype');
    check(Reflect.ownKeys(strategy).length === 0, 'private state is hidden');
    check(Object.prototype.toString.call(strategy) === '[object ' + Strategy.name + ']', 'native tag');
    const descriptor = Object.getOwnPropertyDescriptor(Strategy, 'prototype');
    check(!descriptor.writable && !descriptor.enumerable && !descriptor.configurable, 'constructor prototype descriptor');
    for (const property of ['highWaterMark', 'size']) {
        const accessor = Object.getOwnPropertyDescriptor(Strategy.prototype, property);
        check(typeof accessor.get === 'function' && accessor.get.length === 0, 'getter arity');
        check(accessor.set === undefined && accessor.enumerable && accessor.configurable, 'getter descriptor');
        for (const fake of [{}, Object.create(Strategy.prototype), new Proxy(strategy, {}), null]) {
            check(typeError(() => accessor.get.call(fake)), 'native receiver brand');
        }
    }
    const other = Strategy === CountQueuingStrategy ? ByteLengthQueuingStrategy : CountQueuingStrategy;
    const getter = Object.getOwnPropertyDescriptor(Strategy.prototype, 'highWaterMark').get;
    check(typeError(() => getter.call(new other({highWaterMark: 7}))), 'different strategy brand');
    check(strategy.size === new Strategy({highWaterMark: 9}).size, 'shared size function');
    check(strategy.size.name === 'size' && !('prototype' in strategy.size), 'size function descriptor');
    check(typeError(() => new strategy.size()), 'size is not constructable');
    check(typeError(() => { strategy.highWaterMark = 9; }), 'readonly getter');
    Object.freeze(strategy);
    check(strategy.highWaterMark === 7, 'frozen internal state');

    class Derived extends Strategy {
        size() { return 2; }
    }
    const derived = new Derived({highWaterMark: 17});
    check(derived instanceof Derived && derived.highWaterMark === 17 && derived.size() === 2, 'subclass construction');
    function Alternate() {}
    const alternate = Reflect.construct(Strategy, [{highWaterMark: 23}], Alternate);
    check(Object.getPrototypeOf(alternate) === Alternate.prototype && getter.call(alternate) === 23, 'new target prototype');
    Object.setPrototypeOf(derived, null);
    check(getter.call(derived) === 17, 'brand survives prototype changes');
    console.log(Strategy.name + ' construction, reflection, and brands passed');
}

const bytes = new ByteLengthQueuingStrategy({highWaterMark: 8}).size;
const count = new CountQueuingStrategy({highWaterMark: 8}).size;
check(bytes.length === 1 && count.length === 0, 'size arity');
check(typeError(() => bytes()) && typeError(() => bytes(null)), 'byte length object coercion');
check(bytes('text') === undefined && bytes({}) === undefined, 'missing byte length');
const marker = {};
check(bytes({byteLength: marker}) === marker, 'byte length without numeric conversion');
check(bytes(new Uint8Array(12)) === 12, 'typed array byte length');
const sentinel = new Error('byte length');
const chunk = {get byteLength() { throw sentinel; }};
try { bytes(chunk); throw new Error('missing size exception'); }
catch (error) { check(error === sentinel, 'size exception identity'); }
for (const input of [undefined, null, 'text', {}, chunk]) {
    check(count(input) === 1, 'count ignores chunks');
}
console.log('size callbacks passed');
