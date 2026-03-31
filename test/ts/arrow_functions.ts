// Arrow functions with type annotations
const multiply = (a: number, b: number): number => a * b;
const square = (x: number): number => x * x;
const identity = (s: string): string => s;

console.log(multiply(6, 7));
console.log(square(9));
console.log(identity("test"));
