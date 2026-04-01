// Phase 3.5: Type inference enhancements

// --- 1. Return type propagation from call targets ---
// foo returns INT; let x = foo() → x inferred as INT, native arithmetic
function square(n: number): number {
    return n * n;
}
let a = square(4);
console.log(a + 1);        // 17  (INT + INT → INT native)
console.log(a * 2);        // 32

// --- 2. Conditional expression type ---
// Both branches INT → result is INT, native arithmetic valid
function pickInt(flag: boolean): number {
    return flag ? 10 : 20;
}
console.log(pickInt(true));   // 10
console.log(pickInt(false));  // 20

// Both branches FLOAT
function pickFloat(flag: boolean): number {
    return flag ? 1.5 : 2.5;
}
console.log(pickFloat(true));  // 1.5
console.log(pickFloat(false)); // 2.5

// --- 3. typeof narrowing ---
// val is boxed (any); inside typeof === "number" branch, val should be native float
function addIfNumber(val: any, extra: number): number {
    if (typeof val === "number") {
        return val + extra;  // val narrowed to FLOAT in this branch
    }
    return extra;
}
console.log(addIfNumber(5, 10));    // 15
console.log(addIfNumber("x", 10)); // 10
console.log(addIfNumber(3.5, 1));   // 4.5

// typeof !== narrowing (else branch gets narrowing)
function notNumber(val: any): string {
    if (typeof val !== "number") {
        return "not a number";
    }
    return "is a number";
}
console.log(notNumber("hi"));  // not a number
console.log(notNumber(42));    // is a number

// --- 4. Call-site propagation: function called with literal args ---
function double(x: number): number {
    return x * 2;
}
console.log(double(7));   // 14
console.log(double(3.5)); // 7
