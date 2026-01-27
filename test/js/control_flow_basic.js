// Test if statement
var x = 10;
if (x > 5) {
    console.log("x is greater than 5");
}

// Test if-else statement
var y = 3;
if (y > 5) {
    console.log("y is greater than 5");
} else {
    console.log("y is not greater than 5");
}

// Test nested if-else
var z = 7;
if (z > 10) {
    console.log("z is greater than 10");
} else if (z > 5) {
    console.log("z is greater than 5 but not greater than 10");
} else {
    console.log("z is 5 or less");
}

// Test while loop
var i = 0;
var sum = 0;
while (i < 5) {
    sum = sum + i;
    i = i + 1;
}
console.log(sum);

// Test for loop
var total = 0;
for (var j = 0; j < 10; j = j + 1) {
    total = total + j;
}
console.log(total);

// Test for loop with condition logic
var evenSum = 0;
for (var k = 0; k < 10; k = k + 1) {
    if (k % 2 == 0) {
        evenSum = evenSum + k;
    }
}
console.log(evenSum);

// Test nested loops
var product = 0;
for (var a = 1; a < 4; a = a + 1) {
    for (var b = 1; b < 4; b = b + 1) {
        product = product + a * b;
    }
}
console.log(product);
