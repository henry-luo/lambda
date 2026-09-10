// D2.4.3: storage conversion retains JS Number, Symbol and coercion behavior.
const types = [Int8Array, Uint8Array, Int16Array, Uint16Array, Int32Array,
    Uint32Array, Float32Array, Float64Array, Uint8ClampedArray];
const inputs = [0, -0, 3.9, -4.9, 4294967297, -4294967297, 2147483648,
    -2147483649, NaN, Infinity, -Infinity, 1.23456789, 5e-324];
function encode(value) { return Object.is(value, -0) ? '-0' : String(value); }
function fill(array, values) {
    for (let i = 0; i < values.length; i++) array[i] = values[i];
    let result = '';
    for (let i = 0; i < values.length; i++) result += encode(array[i]) + ',';
    return result;
}
for (const Type of types) {
    const array = new Type(inputs.length + 2);
    const sub = array.subarray(1, inputs.length + 1);
    console.log(Type.name, fill(sub, inputs));
    console.log(array[0], array[array.length - 1], sub[-1], sub[0.5], sub[Infinity]);
    let count = 0;
    sub[0] = {valueOf() { count++; return 258.5; }};
    console.log(encode(sub[0]), count);
    try { sub[0] = Symbol('number'); } catch (e) { console.log(e instanceof TypeError); }
    try { sub[0] = 1n; } catch (e) { console.log(e instanceof TypeError); }
    array.buffer.transfer();
    console.log(sub[0]);
}
const rab = new ArrayBuffer(16, {maxByteLength: 32});
const live = new Int16Array(rab);
const fixed = new Int16Array(rab, 4, 4);
let i = 1;
fixed[i] = {valueOf() { rab.resize(2); return 7; }};
console.log(live.length, fixed[i]);
rab.resize(32);
live[i] = 65537;
console.log(live.length, live[i], fixed[i]);
const shared = new Int32Array(new SharedArrayBuffer(8));
shared[i] = 4294967295;
console.log(shared[i]);
let key = 0;
const target = new Uint16Array(2);
target[key] = (key = 1, 65539);
console.log(target[0], target[1]);
