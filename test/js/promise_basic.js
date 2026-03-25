// Promise basic tests - v14

// Test 1: Promise.resolve
var p1 = Promise.resolve(42);
p1.then(function(value) {
    console.log(value);
});

// Test 2: Promise.reject + catch
var p2 = Promise.reject("error");
p2.catch(function(reason) {
    console.log(reason);
});

// Test 3: new Promise with executor
var p3 = new Promise(function(resolve, reject) {
    resolve("hello");
});
p3.then(function(value) {
    console.log(value);
});

// Test 4: Promise.resolve chaining
Promise.resolve(10).then(function(x) {
    console.log(x * 2);
});

// Test 5: Promise.all with resolved values
var pa = Promise.resolve(1);
var pb = Promise.resolve(2);
var pc = Promise.resolve(3);
Promise.all([pa, pb, pc]).then(function(values) {
    console.log(values.length);
    console.log(values[0]);
    console.log(values[1]);
    console.log(values[2]);
});
0;
