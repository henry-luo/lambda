function check(name, value) { console.log(name + ':' + Boolean(value)); }
function cycle(closed) {
    let channel = new BroadcastChannel('lifetime');
    channel.addEventListener('message', function(event) {
        check('retained-delivery', event.target === channel && event.data.value === 42);
    }, { once: true });
    channel.onmessageerror = function() { return channel; };
    if (!closed) channel.onmessageerror = null;
    if (closed) channel.close();
    return new WeakRef(channel);
}
let closed = cycle(true);
let open = cycle(false);
let source = new BroadcastChannel('lifetime');
let circular = { value: 42 };
circular.self = circular;
source.postMessage(circular);
gc();
setTimeout(function() {
    gc();
    check('closed-cycle-collected', closed.deref() === undefined);
    check('once-listener-released', open.deref() === undefined);
    source.close();
    check('gc-done', true);
}, 0);
