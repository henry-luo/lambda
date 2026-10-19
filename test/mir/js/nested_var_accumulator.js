function sumRows() {
    var result = 0.0;
    for (var row = 0; row < 4; row++) {
        var total = 0.0;
        for (var k = 1; k < 8; k++) total += 1.0 / (k * k);
        result += total;
    }
    return result;
}
function outside(n) {
    for (var i = 0; i < n; i++) { var total = 3; total += i; }
    return total;
}
function skipped(n) {
    var seen = [];
    for (var i = 0; i < n; i++) {
        if (i === 0) { seen.push(total); continue; }
        if (i === 1) var total = 2;
        seen.push(total);
    }
    return seen;
}
function captured() {
    var read;
    for (var i = 0; i < 2; i++) {
        var total = 0;
        read = () => total;
        total += i;
    }
    return read();
}
function widened() {
    var result = [];
    for (var i = 0; i < 2; i++) {
        var total = 0;
        if (i) total = 'x';
        total += 1;
        result.push(total);
    }
    return result.join(',');
}
function selfRead() {
    for (var i = 0; i < 1; i++) { var total = total + 1; return total; }
}
function exceptional() {
    try { for (var i = 0; i < 1; i++) { var total = fail(); } }
    catch (error) { return total; }
}
function fail() { throw 1; }
function special(value) {
    for (var i = 0; i < 1; i++) { var total = value; total *= 1; return total; }
}
function evaluated() {
    for (var i = 0; i < 1; i++) { var total = 2; eval('total = "x"'); return total + 1; }
}
if (Math.abs(sumRows() - 6.04718820861678) > 1e-12 ||
    outside(0) !== undefined || outside(2) !== 4 ||
    skipped(3).join(',') !== ',2,2' || captured() !== 1 || widened() !== '1,x1' ||
    !Number.isNaN(selfRead()) || exceptional() !== undefined || evaluated() !== 'x1' ||
    !Object.is(special(-0), -0) || !Number.isNaN(special(NaN)) || special(Infinity) !== Infinity)
    throw new Error('nested var lifetime');
console.log('nested var: PASS');
