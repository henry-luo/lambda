// typed wrappers must preserve runtime defaults, rest, patterns and closure edges.
function plus(a: number, b: number = a + 1): number {
    return a + b;
}
console.log(plus.length);
console.log(plus(4));
console.log(plus(4, 2));

function sum(a: number, ...rest: number[]): number {
    for (const value of rest) a += value;
    return a;
}
console.log(sum.length);
console.log(sum(1, 2, 3));

function read({x}: {x: number} = {x: 7}): number {
    return x;
}
console.log(read.length);
console.log(read());
console.log(read({x: 9}));

function pick([first, ...rest]: number[]): number {
    return first + rest.length;
}
console.log(pick([10, 20, 30]));

function make(a: number, fn: () => number = () => a): () => number {
    a++;
    console.log(fn.name);
    return fn;
}
const saved = make(41);
console.log(saved());

function before(x: number = y, y: number = 1): number {
    return x + y;
}
try {
    before();
} catch (error) {
    console.log(error instanceof ReferenceError);
}
