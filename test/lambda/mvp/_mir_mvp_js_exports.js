// each public arity exercises the same module-owned computation and bridge.
let offset = 41;
function total(values) {
    let result = offset;
    for (let i = 0; i < values.length; i++) result += values[i];
    return result;
}
export function arity0() { return total([]); }
export function arity1(a) { return total([a]); }
export function arity2(a, b) { return total([a, b]); }
export function arity3(a, b, c) { return total([a, b, c]); }
export function arity4(a, b, c, d) { return total([a, b, c, d]); }
export function arity5(a, b, c, d, e) { return total([a, b, c, d, e]); }
export function arity6(a, b, c, d, e, f) { return total([a, b, c, d, e, f]); }
export function arity7(a, b, c, d, e, f, g) { return total([a, b, c, d, e, f, g]); }
export function arity8(a, b, c, d, e, f, g, h) { return total([a, b, c, d, e, f, g, h]); }
export function fractional(value) { return value + 0.000000000000001; }
export function describe(value) { return 'js:' + value + ':' + offset; }
export function step() { offset += 1; return offset; }
export function rejected() { throw new Error('guest failure'); }
