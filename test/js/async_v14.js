// Async/await and Promise chaining tests - v14

// Test 1: Promise chain with .then
Promise.resolve(5)
    .then(function(x) { return x * 2; })
    .then(function(x) {
        console.log(x);
    });

// Test 2: Promise.reject with .catch
Promise.reject("oops")
    .catch(function(err) {
        console.log(err);
    });

// Test 3: new Promise with delayed resolve
var p = new Promise(function(resolve, reject) {
    resolve(100);
});
p.then(function(val) {
    console.log(val);
});

// Test 4: Promise.race - first resolved wins
var fast = Promise.resolve("fast");
var slow = Promise.resolve("slow");
Promise.race([fast, slow]).then(function(winner) {
    console.log(winner);
});

// Test 5: Promise.any - first fulfilled wins
Promise.any([Promise.reject("err1"), Promise.resolve("ok")]).then(function(val) {
    console.log(val);
});

// Test 6: setTimeout with longer delay
var order = [];
setTimeout(function() {
    console.log("timer1");
}, 10);
setTimeout(function() {
    console.log("timer2");
}, 5);
console.log("sync");

// Test 7: clearTimeout
var tid = setTimeout(function() {
    console.log("should not print");
}, 50);
clearTimeout(tid);

0;
