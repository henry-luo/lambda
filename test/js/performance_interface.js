var Interface = Performance;
console.log(typeof Interface === 'function' && Interface.name === 'Performance');
console.log(Interface.length === 0);
console.log(Object.getPrototypeOf(performance) === Interface.prototype);
console.log(performance instanceof Interface);
console.log(Object.prototype.toString.call(performance) === '[object Performance]');
console.log(performance instanceof EventTarget && Object.getPrototypeOf(Interface.prototype) === EventTarget.prototype);
var entries = Interface.prototype.getEntries;
console.log(entries === performance.getEntries && entries.length === 0);
console.log(Array.isArray(entries.call(performance)));
console.log(Interface.prototype.getEntriesByName.call(performance, 'missing-entry').length === 0);
console.log(Interface.prototype.getEntriesByType.call(performance, 'missing-type').length === 0);
var before = Interface.prototype.now.call(performance);
console.log(typeof before === 'number' && before >= 0 && performance.now() >= before);
try { new Interface(); console.log(false); }
catch (error) { console.log(error instanceof TypeError); }
Interface.prototype.getEntries = function () { return [42]; };
console.log(performance.getEntries()[0] === 42);
Interface.prototype.getEntries = entries;
console.log('PERFORMANCE_INTERFACE_DONE');
