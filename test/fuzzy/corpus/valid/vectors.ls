// Vector arithmetic
let v1 = [1, 2, 3, 4, 5];
let v2 = [10, 20, 30, 40, 50];

// Scalar broadcast
let scaled = v1 * 2;
let offset = v1 + 100;

// Element-wise
let added = v1 + v2;
let multiplied = v1 * v2;

// System functions
let total = sum(v1);
let average = avg(v1);
let d = dot(v1, v2);
let n = norm(v1);
let cs = cumsum(v1);

[total, average, d, n]
