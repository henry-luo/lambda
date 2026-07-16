function tinyReturn() {
    const value = Number.MIN_VALUE;
    gc();
    return value;
}

function relayTiny(depth) {
    if (depth === 0) return tinyReturn();
    return relayTiny(depth - 1);
}

function mixedReturn(tiny) {
    return tiny ? Number.MIN_VALUE : "ready";
}

function overwriteProbe() {
    const returned = tinyReturn();
    tinyReturn();
    return returned;
}

function retBool() { return true; }
function retUndefined() { return undefined; }
function retNull() { return null; }
function retCompactInt() { return 42; }
function retString() { return "ready"; }
function retObject() { return {ready: true}; }
function retArray() { return [1, 2, 3]; }
function retFunction() { return retBool; }
function retInlineDouble() { return 1.25; }
function retPositiveZero() { return 0; }
function retNegativeZero() { return -0; }
function retNaN() { return NaN; }
function retPositiveInfinity() { return Infinity; }
function retNegativeInfinity() { return -Infinity; }
function retNegativeTiny() {
    const value = -Number.MIN_VALUE;
    gc();
    return value;
}

function rootedLocal() {
    const value = {answer: 42};
    gc();
    return value.answer;
}

function makeTinyCell() {
    let value = Number.MIN_VALUE;
    return {
        read() { gc(); return value; },
        write(next) { value = next; gc(); return value; }
    };
}

function* tinyGenerator() {
    let value = Number.MIN_VALUE;
    yield value;
    gc();
    value = -Number.MIN_VALUE;
    return value;
}

function* abruptGenerator() {
    yield 1;
    try {
        yield 2;
    } finally {
        gc();
        yield 3;
    }
}

async function tinyAsync() {
    let value = Number.MIN_VALUE;
    await new Promise(resolve => setTimeout(() => resolve(1), 0));
    gc();
    return value;
}

const cell = makeTinyCell();
const generator = tinyGenerator();
console.log(tinyReturn() === Number.MIN_VALUE);
console.log(relayTiny(8) === Number.MIN_VALUE);
console.log(mixedReturn(true) === Number.MIN_VALUE && mixedReturn(false) === "ready");
console.log(overwriteProbe() === Number.MIN_VALUE);
console.log(retBool() && retString() === "ready" && retInlineDouble() === 1.25);
console.log(Object.is(retPositiveZero(), 0) && Object.is(retNegativeZero(), -0));
console.log(retUndefined() === undefined && retNull() === null &&
    retCompactInt() === 42 && retObject().ready && retArray().length === 3 &&
    retFunction() === retBool);
console.log(retNaN() !== retNaN() && retPositiveInfinity() === Infinity &&
    retNegativeInfinity() === -Infinity && retNegativeTiny() === -Number.MIN_VALUE);
console.log(rootedLocal() === 42);
gc();
console.log(cell.read() === Number.MIN_VALUE);
console.log(cell.write(-Number.MIN_VALUE) === -Number.MIN_VALUE);
console.log(generator.next().value === Number.MIN_VALUE);
gc();
console.log(generator.next().value === -Number.MIN_VALUE);
const abrupt = abruptGenerator();
abrupt.next();
abrupt.next();
gc();
console.log(abrupt.throw(new Error("abrupt")).value === 3);
try {
    abrupt.next();
} catch (error) {
    console.log(error.message === "abrupt");
}
const asyncResult = tinyAsync();
gc();
asyncResult.then(value => console.log(value === Number.MIN_VALUE));

try {
    throw Number.MIN_VALUE;
} catch (value) {
    gc();
    console.log(value === Number.MIN_VALUE);
}
