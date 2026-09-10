// Compile-time shape hints must remain guesses across aliases and re-entry.
function makeRecord(n) { return { amount: n, next: null }; }
function readRecord(record) { const alias = record; return alias.amount; }
function returnRecord(record) { return record; }
let record = makeRecord(3);
console.log(readRecord(returnRecord(record)));
record.amount += 4;
console.log(record.amount, record.next);
record.amount = -0;
console.log(Object.is(record.amount, -0));
record.amount = 5e-324;
console.log(record.amount === 5e-324);
record.amount = 'changed';
console.log(readRecord(record), makeRecord(9).amount);
delete record.amount;
console.log(readRecord(record));
let writes = 0;
Object.defineProperty(record, 'amount', {
    get() { return 27; }, set(v) { writes += v; }, configurable: true
});
record.amount = 2;
console.log(readRecord(record), writes);
function replaceDescriptor(target) {
    Object.defineProperty(target, 'amount', { value: 11, writable: false });
    return 99;
}
const frozen = makeRecord(1);
(function () {
    'use strict';
    try { frozen.amount = replaceDescriptor(frozen); }
    catch (e) { console.log(e instanceof TypeError, frozen.amount); }
})();
let rebound = makeRecord(4);
rebound = { get amount() { return 'getter'; } };
console.log(readRecord(rebound));
rebound = 7;
console.log(readRecord(rebound));
const arrow = value => ({ arrowField: value });
function viaArrow(n) { return arrow(n); }
console.log(viaArrow(8).arrowField);

// The kernel owns holes, descriptors, scalar homes and non-index keys.
const values = [1, 2, 3];
let index = 1;
values[index] += 7;
console.log(values[index]);
delete values[index];
console.log(values[index], index in values);
values[index] = 'refilled';
console.log(values[index]);
Object.defineProperty(values, '1', { get() { return 12; }, configurable: true });
console.log(values[index]);
const wide = [5e-324, -0, NaN, Infinity];
console.log(wide[0] === 5e-324, Object.is(wide[1], -0), Number.isNaN(wide[2]), wide[3]);
const destination = [1, 2];
destination[0] = wide[0];
wide[0] = 10;
console.log(destination[0] === 5e-324);
for (const key of [-1, 0.5, 4294967295, NaN, Infinity]) {
    destination[key] = String(key);
    console.log(destination[key]);
}
const sealed = Object.freeze([10, 20]);
(function () {
    'use strict';
    try { sealed[index] = 1; } catch (e) { console.log(e instanceof TypeError, sealed[index]); }
})();

const ta = new Float64Array(6);
const sub = ta.subarray(2, 5);
sub[index] = 1.25;
console.log(ta[3], sub[index], sub[3]);
sub[index] = -0;
console.log(Object.is(ta[3], -0));
sub[index] = 5e-324;
console.log(ta[3] === 5e-324);
let conversions = 0;
sub[index] = { valueOf() { conversions++; return 7.5; } };
console.log(ta[3], conversions);
ta.buffer.transfer();
console.log(sub[index]);
const rab = new ArrayBuffer(24, { maxByteLength: 48 });
const tracking = new Float64Array(rab);
const fixed = new Float64Array(rab, 8, 2);
tracking[index] = 3.5;
rab.resize(8);
console.log(fixed[0], tracking[index]);
rab.resize(32);
tracking[index] = 4.5;
console.log(fixed[0], tracking[index]);
const byte = new Uint8Array(2);
byte[index] = 257;
console.log(byte[index]);

// Reference bases/keys are evaluated before the RHS and survive suspension.
const suspended = [10, 20, 30];
function* update() {
    let i = 1;
    suspended[i] += yield 'first';
    i = (1 << 1);
    suspended[i] = yield 'second';
    return suspended.join(',');
}
const generator = update();
console.log(generator.next().value, generator.next(4).value, generator.next(9).value);

// An assignment evaluates its reference key before evaluating the RHS.
function changeIndex() {
    const a = [10, 20];
    let i = 0;
    a[i] = (i = 1);
    let j = (0 | 0);
    a[j] += (j = 1);
    return a.join(',');
}
console.log(changeIndex());

// Int32 views sign-extend, publish JS Numbers, and retain live view bounds.
const integers = new Int32Array([-2147483648, -1, 0, 2147483647, 4294967297]);
const integerSub = integers.subarray(1, 4);
for (let k = 0; k < integers.length; k++) console.log(integers[k]);
console.log(integerSub[0], integerSub[2], integerSub[3], Object.is(integers[2], -0));
integers.buffer.transfer();
console.log(integerSub[0]);
