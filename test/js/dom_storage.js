localStorage.clear();
localStorage.setItem('mode', 'dark');
localStorage.setItem(7, 9);
console.log(localStorage.length);
console.log(localStorage.getItem('mode'));
console.log(localStorage.getItem('7'));
console.log(localStorage.key(0));
localStorage.removeItem('mode');
console.log(localStorage.length);
console.log(sessionStorage.getItem('mode') === null);
console.log('STORAGE_DONE');

console.log(localStorage instanceof Storage, sessionStorage instanceof Storage);
console.log(Object.prototype.toString.call(localStorage));
console.log(Object.getPrototypeOf(localStorage) === Storage.prototype);
var set = Object.getOwnPropertyDescriptor(Storage.prototype, 'setItem');
var length = Object.getOwnPropertyDescriptor(Storage.prototype, 'length');
console.log(set.value.length, set.writable, set.enumerable, set.configurable);
console.log(typeof length.get, length.set, length.enumerable, length.configurable);
set.value.call(sessionStorage, 'prototype', 'native');
console.log(Storage.prototype.getItem.call(sessionStorage, 'prototype'));
console.log(length.get.call(sessionStorage));
for (var receiver of [{}, Object.create(Storage.prototype)]) {
    try { set.value.call(receiver, 'x', 'y'); } catch (error) { console.log(error.name); }
    try { length.get.call(receiver); } catch (error) { console.log(error.name); }
}
try { new Storage(); } catch (error) { console.log(error.name); }
var storage = Object.getOwnPropertyDescriptor(globalThis, 'localStorage');
console.log(typeof storage.get, storage.set, storage.get.call(globalThis) === localStorage);
Object.defineProperty(Storage.prototype, 'setItem', {
    value: function(key, value) { set.value.call(this, key, 'wrapped:' + value); },
    writable: true, configurable: true
});
localStorage.setItem('interceptor', 'value');
console.log(localStorage.getItem('interceptor'));
Object.defineProperty(Storage.prototype, 'setItem', set);
