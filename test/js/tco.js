// --- TCO: Tail-Call Optimization Tests ---

// 1. tak function (Takeuchi) — last return is tail-recursive
function tak(x, y, z) {
    if (y >= x) return z;
    const a = tak(x - 1, y, z);
    const b = tak(y - 1, z, x);
    const c = tak(z - 1, x, y);
    return tak(a, b, c);
}
console.log("tak(18,12,6) = " + tak(18, 12, 6));
console.log("tak(22,14,8) = " + tak(22, 14, 8));

// 2. Ackermann function — both branches have tail calls
function ack(m, n) {
    if (m === 0) return n + 1;
    if (n === 0) return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}
console.log("ack(3,4) = " + ack(3, 4));
console.log("ack(3,8) = " + ack(3, 8));

// 3. Tail-recursive factorial with accumulator
function fact(n, acc) {
    if (n <= 1) return acc;
    return fact(n - 1, acc * n);
}
console.log("fact(10,1) = " + fact(10, 1));
console.log("fact(15,1) = " + fact(15, 1));

// 4. Deep tail recursion (would stack overflow without TCO)
function sum_rec(n, acc) {
    if (n <= 0) return acc;
    return sum_rec(n - 1, acc + n);
}
console.log("sum_rec(100000,0) = " + sum_rec(100000, 0));

// 5. Countdown — simple tail recursion
function countdown(n) {
    if (n <= 0) return 0;
    return countdown(n - 1);
}
console.log("countdown(500000) = " + countdown(500000));

// 6. GCD — tail-recursive Euclidean algorithm
function gcd(a, b) {
    if (b === 0) return a;
    return gcd(b, a % b);
}
console.log("gcd(1071,462) = " + gcd(1071, 462));
console.log("gcd(999999,123456) = " + gcd(999999, 123456));

// 7. Non-tail recursive fib (should NOT be affected by TCO)
function rfib(n) {
    if (n <= 1) return n;
    return rfib(n - 1) + rfib(n - 2);
}
console.log("rfib(10) = " + rfib(10));
console.log("rfib(20) = " + rfib(20));
