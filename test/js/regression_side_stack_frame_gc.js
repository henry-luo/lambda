function tinyReturn() {
    const value = Number.MIN_VALUE;
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
