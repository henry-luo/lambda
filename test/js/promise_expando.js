var promise = Promise.resolve(42);
console.log('extensible', Object.isExtensible(promise));
var inherited = 0;
Object.defineProperty(Promise.prototype, 'tracked', {
  set: function(value) { if (this === promise) inherited += value; }, configurable: true
});
promise.tracked = 5;
console.log('inherited setter', inherited, !Object.hasOwn(promise, 'tracked'));
delete Promise.prototype.tracked;
var isolated = Promise.resolve(0);
Object.setPrototypeOf(isolated, null);
isolated.own = 9;
console.log('null prototype', isolated.own, Object.getPrototypeOf(isolated) === null);
promise.status = 'pending';
promise.then(function(value) {
  promise.status = 'fulfilled';
  promise.value = value;
  console.log('settled', promise.status, promise.value);
  var descriptor = Object.getOwnPropertyDescriptor(promise, 'status');
  console.log('descriptor', descriptor.value, descriptor.writable, descriptor.enumerable, descriptor.configurable);
  Object.defineProperty(promise, 'label', {get: function() {return this.status + ':' + this.value;}, configurable: true});
  console.log('accessor', promise.label);
  var key = Symbol('metadata');
  promise[key] = 7;
  if (typeof gc === 'function') gc();
  console.log('symbol', promise[key], Reflect.ownKeys(promise).length);
  console.log('delete', delete promise.status, !('status' in promise));
  Object.preventExtensions(promise);
  console.log('sealed', !Object.isExtensible(promise), Reflect.defineProperty(promise, 'extra', {value: 1}));
  console.log('PROMISE_EXPANDO_DONE');
});
