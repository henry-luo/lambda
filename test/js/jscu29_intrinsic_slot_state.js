// JSCU29: all intrinsic cache domains share one contiguous realm slot owner.
const generator = function* () {};
console.log(Math.max(2, 9), Array === globalThis.Array,
    typeof Reflect.ownKeys, Int8Array.BYTES_PER_ELEMENT,
    Object.getPrototypeOf(generator).constructor.name);
