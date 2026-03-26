// Generator basic tests

// --- Test 1: Simple generator with yield ---
function* simpleGen() {
    yield 1;
    yield 2;
    yield 3;
}

const g1 = simpleGen();
console.log("t1:", g1.next().value, g1.next().value, g1.next().value, g1.next().done);

// --- Test 2: Generator with return ---
function* genWithReturn() {
    yield 10;
    return 20;
}

const g2 = genWithReturn();
const r2a = g2.next();
const r2b = g2.next();
console.log("t2:", r2a.value, r2a.done, r2b.value, r2b.done);

// --- Test 3: Generator with parameters ---
function* genWithParams(start, step) {
    yield start;
    yield start + step;
    yield start + step * 2;
}

const g3 = genWithParams(10, 5);
console.log("t3:", g3.next().value, g3.next().value, g3.next().value);

// --- Test 4: Generator with loop ---
function* counter(n) {
    let i = 0;
    while (i < n) {
        yield i;
        i = i + 1;
    }
}

const g4 = counter(4);
console.log("t4:", g4.next().value, g4.next().value, g4.next().value, g4.next().value, g4.next().done);

// --- Test 5: for...of with generator ---
function* threeItems() {
    yield "a";
    yield "b";
    yield "c";
}

let result5 = "";
for (const item of threeItems()) {
    result5 = result5 + item;
}
console.log("t5:", result5);

// --- Test 6: Generator with conditional ---
function* evenOrOdd(n) {
    let i = 0;
    while (i < n) {
        if (i % 2 === 0) {
            yield i;
        }
        i = i + 1;
    }
}

const g6 = evenOrOdd(6);
let result6 = [];
for (const v of evenOrOdd(6)) {
    result6.push(v);
}
console.log("t6:", result6.join(","));

// --- Test 7: Multiple generator instances ---
function* idGen() {
    let id = 1;
    while (true) {
        yield id;
        id = id + 1;
    }
}

const gen_a = idGen();
const gen_b = idGen();
console.log("t7:", gen_a.next().value, gen_b.next().value, gen_a.next().value, gen_b.next().value);

// --- Test 8: Generator with closure capture ---
function makeGen(multiplier) {
    return function*() {
        yield 1 * multiplier;
        yield 2 * multiplier;
        yield 3 * multiplier;
    };
}

const g8 = makeGen(10)();
console.log("t8:", g8.next().value, g8.next().value, g8.next().value);
