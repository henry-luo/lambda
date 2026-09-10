function nativeRec(n) {
    if (n <= 0) return 0.5;
    return (nativeRec(n - 1) + 1) * 1;
}
console.log('native-recursive', nativeRec(8));
function nativeLeaf(n) {
    if (n < 0) throw 'native-leaf';
    return n * 2;
}
console.log(nativeLeaf(3));
try { nativeLeaf(-1); } catch (e) { console.log(e); }
function nativeStep(n) {
    if (n < 0) throw 'native';
    if (n > 0) return nativeStep(n - 1) + 1;
    return 0.5;
}
function boxedStep(value) {
    return value === null ? { empty: true } : value;
}
function boxedCaller(value) { return boxedStep(value); }
console.log(nativeStep(5));
try { nativeStep(-1); } catch (e) { console.log(e); }
console.log(boxedCaller(Number.MIN_VALUE) === Number.MIN_VALUE);
