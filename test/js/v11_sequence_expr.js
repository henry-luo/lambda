// v11: Sequence expressions (comma operator)

// Basic comma operator
let a = (1, 2, 3);
console.log("comma result:", a);

// Comma in for loop
let sum = 0;
for (let i = 0, j = 10; i < 5; i++, j--) {
    sum += i + j;
}
console.log("loop sum:", sum);

// Nested comma
let b = (10, 20, 30, 40);
console.log("nested comma:", b);
