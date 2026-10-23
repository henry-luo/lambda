function check(value) { if (!value) throw new Error('parameter expression'); console.log(true); }
let source = 'abcdef';
function code(index) { 'use strict'; return index < source.length ? source.charCodeAt(index) : 0; }
check(code(2) === 99 && code(9) === 0);
source = 'XYZ';
check(code(1) === 89);
let reads = 0;
function load(object) { 'use strict'; return object.value; }
const getter = { get value() { reads++; return { number: 9007199254740992 }; } };
check(load(getter).number === 9007199254740992 && reads === 1);
let has = 0;
const proxy = new Proxy({ value: 17 }, { get(target, key) { has++; return target[key]; } });
check(load(proxy) === 17 && has === 1);
function add(value, other) { 'use strict'; return value + other; }
let coercions = 0;
check(add({ valueOf() { coercions++; return 13; } }, 4) === 17 && coercions === 1);
check(add('a', 'b') === 'ab' && add(2n, 3n) === 5n);
function recurse(object, callback) { 'use strict'; return callback(object) + object.value; }
check(recurse({ value: 11 }, object => load(object)) === 22);
let threw = false;
try { load({ get value() { throw new Error('getter failure'); } }); }
catch (error) { threw = error.message === 'getter failure'; }
check(threw);
check(load({ value: 9007199254740992 }) === 9007199254740992);
function missing(value, other) { 'use strict'; return other; }
check(missing(2) === undefined && missing(2, 'kept') === 'kept');
function observed(value) { 'use strict'; return arguments[0]; }
check(observed('kept') === 'kept');
function evaluated(value) { 'use strict'; return eval('value'); }
check(evaluated('kept') === 'kept');
function receiver(value) { 'use strict'; return this.value + value; }
check(receiver.call({ value: 2 }, 3) === 5);
function target(value) { 'use strict'; return new.target ? value : null; }
check(target(3) === null && new target({ value: 7 }).value === 7);
function increment(value) { 'use strict'; return ++value; }
function decrement(value) { 'use strict'; return --value; }
function assigned(value) { 'use strict'; return value = 7; }
check(increment(3) === 4 && decrement(3) === 2 && assigned(3) === 7);
function captured(value) { 'use strict'; return () => value; }
const closure = captured('kept');
check(closure() === 'kept');
function defaulted(value = 'kept') { return value; }
check(defaulted() === 'kept');
function typeofValue(value) { 'use strict'; return typeof value; }
check(typeofValue({}) === 'object' && typeofValue(2) === 'number');
function remove(object) { 'use strict'; return delete object.value; }
const removed = { value: 1 };
check(remove(removed) && !('value' in removed));
let tdz = false;
try {
  function read(value) { 'use strict'; return late + value; }
  read(1);
  let late = 2;
} catch (error) { tdz = error instanceof ReferenceError; }
check(tdz);
class Box {
  #secret = 19;
  constructor(value) { this.value = value; }
  add(other) { return this.value + other; }
  get doubled() { return this.value * 2; }
  secret(other) { return other.#secret; }
}
const box = new Box(7);
check(box.add(3) === 10 && box.doubled === 14 && box.secret(box) === 19);
check(Box.prototype.add.call({ value: 11 }, 2) === 13);
const doubled = Object.getOwnPropertyDescriptor(Box.prototype, 'doubled').get;
check(doubled.call({ value: 13 }) === 26);
let badReceiver = false;
try { doubled.call(null); } catch (error) { badReceiver = error instanceof TypeError; }
check(badReceiver);
console.log('PARAMETER_EXPRESSION_DONE');
