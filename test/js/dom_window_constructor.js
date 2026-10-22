console.log('window:type:' + typeof Window);
console.log('window:brand:' + (window instanceof Window));
console.log('window:event-target:' + (Window.prototype instanceof EventTarget));
var windowDescriptor = Object.getOwnPropertyDescriptor(globalThis, 'window');
console.log('window:descriptor:' + (typeof windowDescriptor.get === 'function' && windowDescriptor.set === undefined && windowDescriptor.enumerable && !windowDescriptor.configurable));
console.log('window:getter:' + (windowDescriptor.get.call(window) === globalThis && windowDescriptor.get.toString().includes('[native code]')));
try { windowDescriptor.get.call({}); console.log('window:receiver:false'); }
catch (error) { console.log('window:receiver:' + (error instanceof TypeError)); }
try { (function(){'use strict';globalThis.window = {};})(); console.log('window:readonly:false'); }
catch (error) { console.log('window:readonly:' + (error instanceof TypeError && window === globalThis)); }
