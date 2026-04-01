// test as_expression and non_null_expression lowering

// as expression with typed const variables
const x: number = 42;
const y = (x as number) + 1;
console.log(y);

// as expression with string
const s: string = "hello";
const t = s as string;
console.log(t);

// non-null assertion — passes through to inner expression
const arr: number[] = [10, 20, 30];
console.log(arr[0]);

// as expression in arithmetic
const n: number = 100;
const doubled = (n as number) * 2;
console.log(doubled);
