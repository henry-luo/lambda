// Microtask ordering tests - Phase 7

// Test 1: Microtasks run before macrotasks (setTimeout)
// Expected: sync, micro, macro
setTimeout(function() { console.log("macro"); }, 0);
Promise.resolve().then(function() { console.log("micro"); });
console.log("sync");

// Test 2: Promise.resolve().then() is deferred (not synchronous)
// Expected: a, b, c
var order = [];
Promise.resolve(1).then(function(v) {
    order.push("b");
    console.log("b");
});
order.push("a");
console.log("a");
// 'c' is printed after microtask flush
Promise.resolve(2).then(function() {
    order.push("c");
    console.log("c");
});

// Test 3: Then chain ordering
// Expected: then1:10, then2:20
Promise.resolve(10).then(function(x) {
    console.log("then1:" + x);
    return x * 2;
}).then(function(x) {
    console.log("then2:" + x);
});

// Test 4: Multiple then on same promise
// Expected: first:42, second:42
var p = Promise.resolve(42);
p.then(function(v) { console.log("first:" + v); });
p.then(function(v) { console.log("second:" + v); });

// Test 5: Reject + catch chain
// Expected: caught:err, recovered:ok
Promise.reject("err").catch(function(e) {
    console.log("caught:" + e);
    return "ok";
}).then(function(v) {
    console.log("recovered:" + v);
});

// Test 6: new Promise with async resolve via setTimeout
// Expected: before, after, resolved:hello
console.log("before");
var p2 = new Promise(function(resolve) {
    setTimeout(function() { resolve("hello"); }, 10);
});
p2.then(function(v) {
    console.log("resolved:" + v);
});
console.log("after");

// Test 7: Finally handler
// Expected: finally_ran, value:99
Promise.resolve(99).finally(function() {
    console.log("finally_ran");
}).then(function(v) {
    console.log("value:" + v);
});

0;
