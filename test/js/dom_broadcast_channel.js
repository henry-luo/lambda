function check(name, value) { console.log(name + ':' + Boolean(value)); }
function throws(name, action, kind) {
    try { action(); check(name, false); }
    catch (error) { check(name, error.name === kind); }
}
throws('requires-new', function() { BroadcastChannel('x'); }, 'TypeError');
throws('requires-name', function() { new BroadcastChannel(); }, 'TypeError');
throws('symbol-name', function() { new BroadcastChannel(Symbol()); }, 'TypeError');
var conversions = 0;
var a = new BroadcastChannel({ toString() { conversions++; return 'channel\0one'; } });
var b = new BroadcastChannel('channel\0one');
var c = new BroadcastChannel('channel\0one');
var other = new BroadcastChannel('channel');
check('name-coercion', conversions === 1 && a.name === 'channel\0one');
check('prototype', a instanceof BroadcastChannel && a instanceof EventTarget);
check('tag', Object.prototype.toString.call(a) === '[object BroadcastChannel]');
check('handlers-null', a.onmessage === null && a.onmessageerror === null);
var nameGetter = Object.getOwnPropertyDescriptor(BroadcastChannel.prototype, 'name').get;
throws('getter-brand', function() { nameGetter.call({}); }, 'TypeError');
throws('method-brand', function() { a.postMessage.call({}, 1); }, 'TypeError');
throws('requires-message', function() { a.postMessage(); }, 'TypeError');
throws('uncloneable', function() { a.postMessage(function() {}); }, 'DataCloneError');
throws('channel-uncloneable', function() { a.postMessage(b); }, 'DataCloneError');
var cycle = {};
cycle.self = cycle;
var copied = structuredClone({ first: cycle, second: cycle });
check('clone-cycles-aliases', copied.first !== cycle && copied.first === copied.second &&
    copied.first.self === copied.first);
check('clone-bigint', structuredClone(12345678901234567890n) === 12345678901234567890n);
var events = [];
var senderEcho = 0;
var unrelated = 0;
a.onmessage = function() { senderEcho++; };
other.onmessage = function() { unrelated++; };
var getters = 0;
var data = { get nested() { getters++; return { number: 7 }; } };
var calls = 0;
var received = 0;
function receivedMessage() {
    if (++received === 4) setTimeout(finish, 0);
}
function removed() { calls += 100; }
b.addEventListener('message', removed);
b.removeEventListener('message', removed);
b.addEventListener('message', function() { calls++; }, { once: true });
b.onmessage = function(event) {
    events.push('b' + event.data.nested.number);
    check('event-metadata', event instanceof MessageEvent && event.target === b &&
        event.currentTarget === b && event.source === null && event.isTrusted &&
        !event.bubbles && !event.cancelable && event.ports.length === 0);
    event.data.nested.number = 99;
    receivedMessage();
};
c.onmessage = function(event) {
    events.push('c' + event.data.nested.number);
    receivedMessage();
};
a.postMessage(data);
check('async-snapshot', events.length === 0 && getters === 1);
if (typeof gc === 'function') gc();
var cancelled = new BroadcastChannel('channel\0one');
cancelled.onmessage = function() { events.push('cancelled'); };
a.postMessage({ nested: { number: 8 } });
cancelled.close();
cancelled.close();
throws('closed-send', function() { cancelled.postMessage(1); }, 'InvalidStateError');
var manual = 0;
cancelled.onmessage = function() { manual++; };
if (typeof gc === 'function') gc();
cancelled.dispatchEvent(new MessageEvent('message', { data: 2 }));
check('closed-dispatch', manual === 1);
class ChildChannel extends BroadcastChannel {}
var child = new ChildChannel('child');
check('subclass', child instanceof ChildChannel && child instanceof BroadcastChannel);
child.close();
function finish() {
    check('delivery-order-isolation', events.join(',') === 'b7,c7,b8,c8');
    check('sender-and-name-isolation', senderEcho === 0 && unrelated === 0);
    check('listener-removal-once', calls === 1);
    check('serialize-once', getters === 1);
    check('closed-delivery-cancelled', manual === 1);
    a.close(); b.close(); c.close(); other.close();
    check('done', true);
}
