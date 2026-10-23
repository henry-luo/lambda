const constructors = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array,
  Uint16Array, Int32Array, Uint32Array, Float32Array, Float64Array];
console.log(constructors.every(Constructor => {
  const values = new Constructor(4);
  let index = 1;
  values[index] = 12;
  return values[index] === 12 && values[4] === undefined;
}));
const tokens = new Uint32Array(1024);
for (let index = 0; index < tokens.length; index++) tokens[index] = index * 3;
let sum = 0;
for (let index = 0; index < tokens.length; index++) sum += tokens[index];
console.log(sum === 1023 * 1024 / 2 * 3);
const values = new Uint32Array(4);
values[-0] = 0xff000001;
values['-0'] = 7;
values['01'] = 9;
console.log(values[0] === 0xff000001 && values[-0] === values[0] && values['-0'] === undefined && values['01'] === 9);
console.log([NaN, Infinity, -1, 1.5, 0xffffffff].every(index => values[index] === undefined));
const order = [];
const key = {toString() {order.push('key'); return '1';}};
const number = {valueOf() {order.push('value'); return 17;}};
values[key] = number;
console.log(values[1] === 17 && order.join(',') === 'key,value');
let observed;
const proxy = new Proxy(values, {
  get(target, key) {observed = key; return Reflect.get(target, key, target);},
  set(target, key, value) {observed = key; return Reflect.set(target, key, value, target);}
});
proxy[2] = 23;
console.log(observed === '2' && proxy[2] === 23 && observed === '2');
const original = new Uint8Array(2), replacement = new Uint8Array(2);
let receiver = original;
receiver[0] = (receiver = replacement, 31);
console.log(original[0] === 31 && replacement[0] === 0);
const buffer = new ArrayBuffer(8, {maxByteLength: 16});
const tracking = new Uint8Array(buffer);
const coercive = {valueOf() {buffer.resize(0); return 12;}};
const assigned = (tracking[0] = coercive);
console.log(assigned === coercive && tracking.length === 0 && tracking[0] === undefined);
buffer.resize(4);
tracking[3] = 19;
console.log(tracking.length === 4 && tracking[3] === 19);
const detachedBuffer = new ArrayBuffer(4), detached = new Uint8Array(detachedBuffer);
detached[0] = {valueOf() {detachedBuffer.transfer(); return 5;}};
console.log(detached.length === 0 && detached[0] === undefined);
const big = new BigInt64Array(2);
big[1] = 1234567890123456789n;
let bigintError = false;
try {big[0] = 1;} catch (error) {bigintError = error instanceof TypeError;}
console.log(big[1] === 1234567890123456789n && bigintError);
const frozen = Object.freeze([1]);
let frozenError = false;
try {(function(){'use strict'; frozen[0] = 2;})()} catch (error) {frozenError = error instanceof TypeError;}
console.log(frozenError && frozen[0] === 1);
let inherited = 0;
const prototype = Object.create(Array.prototype);
Object.defineProperty(prototype, '1', {get() {return 11;}, set(value) {inherited = value;}});
const holes = [];
Object.setPrototypeOf(holes, prototype);
holes[1] = 29;
console.log(inherited === 29 && holes[1] === 11 && !Object.hasOwn(holes, '1'));
const plain = [0, 1];
plain[0] = {marker:true};
plain[1] = 3.5;
console.log(plain[0].marker && plain[1] === 3.5);
console.log('NUMERIC_INDICES_DONE');
