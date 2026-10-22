const proto = Performance.prototype;
const setSize = proto.setResourceTimingBufferSize;
const clear = proto.clearResourceTimings;
console.log(typeof setSize === 'function' && setSize.length === 1);
console.log(typeof clear === 'function' && clear.length === 0);
console.log(setSize.call(performance, 2) === undefined);
console.log(clear.call(performance) === undefined);
console.log(performance.getEntriesByType('resource').length === 0);
for (const receiver of [{}, Object.create(proto), null]) {
  try { setSize.call(receiver, 1); console.log(false); }
  catch (error) { console.log(error instanceof TypeError); }
  try { clear.call(receiver); console.log(false); }
  catch (error) { console.log(error instanceof TypeError); }
}
try { setSize.call(performance); console.log(false); }
catch (error) { console.log(error instanceof TypeError); }
const sentinel = {};
try { setSize.call(performance, {valueOf() { throw sentinel; }}); console.log(false); }
catch (error) { console.log(error === sentinel); }
for (const value of [0, -1, 1.5, NaN, Infinity, undefined, 4294967297]) {
  if (setSize.call(performance, value) !== undefined) throw Error('size conversion');
}
const Observer = PerformanceObserver;
console.log(typeof Observer.prototype.observe === 'function' && Observer.prototype.observe.length === 0);
console.log(typeof PerformanceObserverEntryList.prototype.getEntries === 'function');
let calls = 0;
const observer = new Observer(() => calls++);
Object.freeze(observer);
Observer.prototype.observe.call(observer, {entryTypes:['resource']});
console.log(observer instanceof Observer && calls === 0);
console.log(Observer.prototype.takeRecords.call(observer).length === 0);
console.log(Observer.prototype.disconnect.call(observer) === undefined);
console.log(Observer.supportedEntryTypes.includes('resource') && Object.isFrozen(Observer.supportedEntryTypes));
for (const name of ['observe', 'disconnect', 'takeRecords']) {
  try { Observer.prototype[name].call({}); console.log(false); }
  catch (error) { console.log(error instanceof TypeError); }
}
try { new Observer(3); console.log(false); }
catch (error) { console.log(error instanceof TypeError); }
console.log('PERFORMANCE_RESOURCE_BUFFER_DONE');
