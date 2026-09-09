// JSCU29: typed-array caches are slots in the one realm intrinsic root range.
var base = Object.getPrototypeOf(Int8Array);
var base_prototype = base.prototype;
var int8_prototype = Int8Array.prototype;
var uint16_prototype = Uint16Array.prototype;

gc();

var values = new Int8Array(3);
values[2] = 42;
console.log(base === Object.getPrototypeOf(Int8Array),
    base_prototype === Object.getPrototypeOf(Int8Array).prototype,
    int8_prototype === Int8Array.prototype,
    uint16_prototype === Uint16Array.prototype, values[2]);
