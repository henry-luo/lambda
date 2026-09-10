'use strict';

function nativeRec(n) {
    if (n <= 0) return 0.5;
    return (nativeRec(n - 1) + 1) * 1;
}
console.log('native-recursive', nativeRec(8));
try { nativeRec(20000); } catch (e) { console.log('native-depth', e instanceof RangeError); }
console.log('native-depth-restored', nativeRec(2), nativeRec(2500));
function throwWide(n) { if (n > 0) throw n / 2; return n + 1; }
try { throwWide(Number.MIN_VALUE * 2); } catch (e) {
    boxed({ collect: true });
    console.log('wide-throw', e === Number.MIN_VALUE);
}

function fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
console.log('recursive', fib(12));
function nativeLeaf(n) {
    if (n < 0) throw 'native-leaf';
    return n * 2;
}
console.log('native-leaf', nativeLeaf(3));
try { nativeLeaf(-1); } catch (e) { console.log('native-leaf-error', e); }
console.log('native-leaf-clean', nativeLeaf(4));
function allocatingFinally(n) {
    try { return nativeLeaf(n - 0) * 2; }
    finally { boxed({ keep: [n, 'finally'] }); }
}
try { allocatingFinally(-1); } catch (e) { console.log('native-finally-error', e); }
console.log('native-finally-clean', allocatingFinally(2));

var thrown = { reason: 'native' };
var finalized = 0;
function checked(n) {
    try {
        if (n < 0) throw thrown;
        return n + 0.5;
    } finally {
        finalized++;
    }
}
function caller(n) { return checked(n) + 2; }
var indirect = checked;
console.log('normal', caller(3), indirect(4));
try { caller(-1); } catch (e) { console.log('direct-error', e === thrown); }
try { indirect(-1); } catch (e) { console.log('boxed-error', e === thrown); }
console.log('after-error', caller(1), finalized);

function override(n) {
    try { return checked(n); } finally { return 17.5; }
}
console.log('finally-override', override(-1));

function depth(n) {
    if (n <= 0) return 1;
    return depth(n - 1) + 1;
}
try { depth(20000); } catch (e) { console.log('direct-depth', e instanceof RangeError); }
console.log('depth-restored', depth(10), depth(2500));
var indirectDepth = mixedDepth;
function mixedDepth(n) {
    if (n <= 0) return 1;
    if (n % 2) return indirectDepth(n - 1) + 1;
    return mixedDepth(n - 1) + 1;
}
try { mixedDepth(20000); } catch (e) { console.log('mixed-depth', e instanceof RangeError); }
console.log('mixed-restored', mixedDepth(10), mixedDepth(2500));

// a boxed ABI transports the payload even when it originally belonged to a module slot.
var wide = Number.MIN_VALUE;
function boxed(v) { return v === null ? { empty: true } : v; }
function fromModule() { return boxed(wide); }
function overwrite() { wide = 0; return boxed(1.25); }
var kept = fromModule();
overwrite();
console.log('module-owner', kept === Number.MIN_VALUE, wide === 0);
var saved = [];
for (var i = 0; i < 80; i++) {
    saved.push(boxed(i % 2 ? -Number.MIN_VALUE : Number.MIN_VALUE));
    boxed({ index: i });
}
console.log('owned-results', saved.every(function(v, i) {
    return v === (i % 2 ? -Number.MIN_VALUE : Number.MIN_VALUE);
}));
console.log('special-numbers', Object.is(boxed(-0), -0), Number.isNaN(boxed(NaN)), boxed(Infinity));

function* suspendOwned() {
    var value = boxed(Number.MIN_VALUE);
    yield 1;
    return value;
}
var gen = suspendOwned();
gen.next();
overwrite();
console.log('suspended-owner', gen.next().value === Number.MIN_VALUE);

var coercions = [];
var left = { valueOf: function() { coercions.push('left'); return 7; } };
var right = { valueOf: function() { coercions.push('right'); return 3; } };
console.log('helper-results', left > right, left & right, ~right, typeof left, !left);
console.log('helper-order', coercions.join(','));
console.log('bigint-results', String((1n << 70n) | 3n), String(~1n), String(9n >> 1n));
try { left < Symbol('bad'); } catch (e) { console.log('helper-error', e instanceof TypeError); }
