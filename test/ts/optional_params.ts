// Optional parameters
function optFn(x: number, y?: number): number {
    if (y !== undefined) {
        return x + y;
    }
    return x;
}

console.log(optFn(10));
console.log(optFn(10, 20));
console.log(optFn(5));
