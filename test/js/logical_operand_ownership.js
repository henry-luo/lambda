function check(value) { if (!value) throw new Error('logical operands'); console.log(true); }
let calls = 0;
function right() { calls++; return { number: 9007199254740992 }; }
check((false && right()) === false && calls === 0);
check((true || right()) === true && calls === 0);
check((0 ?? right()) === 0 && calls === 0);
check((true && right()).number === 9007199254740992 && calls === 1);
check((false || right()).number === 9007199254740992 && calls === 2);
check((null ?? right()).number === 9007199254740992 && calls === 3);
const wide = { get value() { calls++; return 9007199254740992; } };
const kept = true && wide.value;
for (let index = 0; index < 128; index++) ({ number: index + 0.25 });
check(kept === 9007199254740992);
const kept_tiny = null ?? ({ get value() { return 5e-324; } }).value;
for (let index = 0; index < 128; index++) ({ number: index + 0.5 });
check(kept_tiny === 5e-324);
let order = '';
function left() { order += 'L'; return { get value() { order += 'G'; return 0; } }; }
check((left().value || (order += 'R', 7)) === 7 && order === 'LGR');
let threw = false;
try { true && (() => { throw new Error('right'); })(); }
catch (error) { threw = error.message === 'right'; }
check(threw);
function below(value, limit) { return value < limit; }
function equal(value, other) { return value === other; }
function inclusive(value) { return value >= -0 && value <= 9007199254740992; }
check(below(5e-324, 1e-323) && !below(NaN, Infinity));
function subnormal(value) { return value >= 5e-324 && value <= 1e-323; }
check(subnormal(5e-324) && subnormal(1e-323) && !subnormal(0));
function literal_pair(unused) { return 5e-324 < 1e-323; }
check(literal_pair(0));
check(equal(-0, 0) && !equal(NaN, NaN) && equal(Infinity, Infinity));
check(inclusive(-0) && inclusive(9007199254740992) && !inclusive(Infinity));
let conversions = 0;
check(below({ valueOf() { conversions++; return 3; } }, 4) && conversions === 1);
check(below(2n, 3n) && !equal(2n, 2));
console.log('LOGICAL_OPERANDS_DONE');
