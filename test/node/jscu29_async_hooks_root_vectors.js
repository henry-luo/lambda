const { createHook, AsyncResource } = require('async_hooks');

let init_count = 0;
for (let i = 0; i < 257; i++) {
    createHook({ init() { init_count++; } }).enable();
}
new AsyncResource('JSCU29_HOOKS');
console.log(init_count);

let destroy_count = 0;
createHook({ destroy() { destroy_count++; } }).enable();
for (let i = 0; i < 1025; i++) {
    new AsyncResource('JSCU29_DESTROY');
}
gc();
console.log(destroy_count);
