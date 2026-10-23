function check(value, label) {
    if (!value) throw new Error(label);
    console.log(true);
}
function digit(code) { return code >= 48 && code <= 57; }
check(digit(48) && digit(57) && !digit(47) && !digit(58), 'numeric bounds');
check(!digit(NaN) && !digit(Infinity) && !digit(-0) && !digit(true), 'special numbers');
check(digit('50'), 'string coercion fallback');
let conversions = 0;
check(digit({ valueOf() { conversions++; return 49; } }) && conversions === 2, 'observable coercion');
let threw = false;
try {
    digit({ valueOf() { if (++conversions === 4) throw new Error('stop'); return 49; } });
} catch (error) { threw = error.message === 'stop'; }
check(threw && conversions === 4, 'throwing coercion');
check(digit(50n), 'BigInt fallback');
function equal(a, b) { return a === b; }
check(equal(-0, 0) && !equal(NaN, NaN) && !equal(0, false), 'strict equality');
function identity(value) { return value; }
const wide = identity(9007199254740992);
check(wide === 9007199254740992 && identity(Infinity) === Infinity, 'numeric return ownership');
function choose(x, y) { return x >= 0 ? y : !(y >= 1); }
check(choose(0, 9007199254740992) === wide && choose(-1, 0) === true, 'conditional return');
check(!digit(), 'missing argument');
let limit = 50;
function below(value) { return value < limit; }
const before = below(49);
limit = 48;
check(before && !below(49), 'captured binding');
function assigned(value) { return (value = 7) === value; }
check(assigned({}), 'parameter mutation');
let hasParameter = 0, withResult;
const scope = new Proxy({}, { has(target, name) { if (name === 'value') hasParameter++; return false; } });
function within(value) { return value > 1; }
with (scope) { withResult = within(2); }
check(withResult && hasParameter === 0, 'outer object environment');
function defaulted(value = 50) { return value >= 48 && value <= 57; }
check(defaulted(), 'parameter initialization');
let tdz = false;
try {
    function readLate(value) { return value < late; }
    readLate(1);
    let late = 2;
} catch (error) { tdz = error instanceof ReferenceError; }
check(tdz, 'captured TDZ');
const instance = new digit(50);
check(typeof instance === 'object' && instance instanceof digit, 'construct return');
check(digit(50, { unused: true }), 'unused rooted argument');
function shadow(arguments) { return arguments > 1; }
check(shadow(2) && shadow('2'), 'parameter shadow');
console.log('NUMERIC_LEAF_DONE');
