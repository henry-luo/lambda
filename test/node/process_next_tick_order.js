// process.nextTick should run before Promise microtasks at an async checkpoint.

setTimeout(function() {
    console.log("timer");
}, 0);

Promise.resolve().then(function() {
    console.log("promise");
});

process.nextTick(function() {
    console.log("tick");
});

process.nextTick(function(a, b) {
    console.log("tick-args:" + a + ":" + b);
}, "a", "b");

console.log("sync");

0;
