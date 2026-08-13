var queue_count = 0;
function enqueue(i) {
    if (i === 0) return;
    queueMicrotask(function() { queue_count++; });
    enqueue(i - 1);
}
enqueue(1100);

var reaction_count = 0;
var resolve_pending;
var pending = new Promise(function(resolve) { resolve_pending = resolve; });
for (var j = 0; j < 32; j++) {
    pending.then(function() { reaction_count++; });
}
resolve_pending(1);

setTimeout(function() {
    console.log(queue_count);
    console.log(reaction_count);
}, 0);
