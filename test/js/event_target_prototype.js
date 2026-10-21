var proto = EventTarget.prototype;
console.log(typeof proto.addEventListener === 'function' && typeof proto.removeEventListener === 'function' && typeof proto.dispatchEvent === 'function');
var a = new EventTarget(), b = new EventTarget();
console.log(a.addEventListener === proto.addEventListener && !Object.hasOwn(a, 'addEventListener'));
var calls = 0, receiver;
function listener(event) { calls++; receiver = this; }
proto.addEventListener.call(a, 'test', listener);
console.log(proto.dispatchEvent.call(b, new Event('test')) && calls === 0);
console.log(proto.dispatchEvent.call(a, new Event('test')) && calls === 1 && receiver === a);
proto.removeEventListener.call(a, 'test', listener);
proto.dispatchEvent.call(a, new Event('test'));
console.log(calls === 1);
console.log('EVENT_TARGET_PROTOTYPE_DONE');
