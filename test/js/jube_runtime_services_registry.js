const asyncHooks = require('async_hooks');
const traceEvents = require('trace_events');

console.log(asyncHooks === require('node:async_hooks'), asyncHooks.default === asyncHooks);
console.log(typeof asyncHooks.AsyncLocalStorage, typeof asyncHooks.AsyncResource, typeof asyncHooks.createHook);
console.log(traceEvents === require('node:trace_events'), traceEvents.default === traceEvents);
console.log(typeof traceEvents.createTracing, typeof traceEvents.getEnabledCategories);
