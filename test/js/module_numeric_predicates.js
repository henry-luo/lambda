import {counter, bump} from './interp_esm/dep.mjs';
export {};
function check(value) { if (!value) throw new Error('module numeric predicate'); console.log(true); }
function range(value) { return value >= 0 && value < 128; }
const equal = function(left, right) { return left === right; };
function identity(value) { return value; }
function invert(value) { return !value; }
function coercingEqual(left, right) { return left == right; }
let untouched = 17;
const source = range.toString();
for (let index = 0; index < 32; index++) {
  range(index); equal(index, index); identity(index); invert(index); coercingEqual(index, index);
}
check(range(64) && !range(128) && !range(-1));
check(!range(NaN) && !range(Infinity) && range(-0));
check(!range() && range('64'));
check(range(64n) && !range(2n ** 100n));
let coercions = 0;
check(range({valueOf() { coercions++; return 64; }}) && coercions === 2);
const marker = new Error('coercion marker');
let caught = false;
try { range({valueOf() { throw marker; }}); } catch (error) { caught = error === marker; }
check(caught);
caught = false;
try { range(Symbol('value')); } catch (error) { caught = error instanceof TypeError; }
check(caught);
check(!equal(NaN, NaN) && equal(0, -0));
check(!equal(1, 1n) && equal(1n, 1n));
check(coercingEqual(1n, '1') && coercingEqual(1n, true) && !coercingEqual(1n, 1.5));
check(invert(0n) && !invert(1n) && invert(0) && !invert('kept'));
check(!invert({valueOf() { throw marker; }}));
const object = {};
check(equal(object, object) && !equal(object, {}));
const values = [identity(9007199254740992), identity(5e-324), identity(-0)];
if (typeof gc === 'function') gc();
check(values[0] === 9007199254740992 && values[1] === 5e-324 && 1 / values[2] === -Infinity);
check(identity(object) === object && identity(11n) === 11n && identity() === undefined);
check(range.toString() === source && range.name === 'range' && range.length === 1);
check(untouched === 17 && counter === 40);
bump();
function imported(value) { return value < counter; }
for (let index = 0; index < 8; index++) imported(40);
check(imported(40) && counter === 41 && untouched === 17);
export default function(value) { return value < 128; }
console.log('MODULE_NUMERIC_PREDICATES_DONE');
