var firstTime = 0;
requestAnimationFrame(function (time) {
    firstTime = time;
    cancelAnimationFrame(cancelled);
    requestAnimationFrame(function (later) {
        console.log("next-frame:" + (later > firstTime));
    });
});
var cancelled = requestAnimationFrame(function () { console.log("cancelled callback ran"); });
var order = [];
requestAnimationFrame(function (time) {
    order.push("first");
    queueMicrotask(function () {
        order.push("microtask");
        cancelAnimationFrame(microtaskCancelled);
    });
    console.log("same-frame:" + (time === firstTime));
});
requestAnimationFrame(function () {
    order.push("second");
    console.log(order.join(","));
});
var microtaskCancelled = requestAnimationFrame(function () {
    console.log("microtask-cancelled callback ran");
});
