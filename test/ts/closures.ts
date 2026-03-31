// Closures with type annotations
function makeCounter(): () => number {
    let count: number = 0;
    return (): number => {
        count = count + 1;
        return count;
    };
}

const counter = makeCounter();
console.log(counter());
console.log(counter());
console.log(counter());
