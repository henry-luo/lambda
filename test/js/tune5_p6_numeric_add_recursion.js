function fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

function ack(m, n) {
    if (m === 0) return n + 1;
    if (n === 0) return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}

function add(a, b) {
    return a + b;
}

function mixed_base(n) {
    if (n === 0) return "x";
    return mixed_base(n - 1) + 1;
}

console.log("fib", fib(10));
console.log("ack", ack(3, 4));
console.log("num", add(2, 3));
console.log("left-string", add("2", 3));
console.log("right-string", add(2, "3"));
console.log("mixed-base", mixed_base(2));
