// events module basic tests
import events from 'events';

// create emitter
var ee = events.EventEmitter();

// on + emit
events.on(ee, 'data', function(msg) { console.log('got: ' + msg); });
console.log(events.listenerCount(ee, 'data'));
events.emit(ee, 'data', 'hello');

// once - listener fires once then removed
events.once(ee, 'end', function() { console.log('ended'); });
events.emit(ee, 'end');
events.emit(ee, 'end');
console.log(events.listenerCount(ee, 'end'));

// off - remove listener
var count = 0;
var fn = function() { count++; };
events.on(ee, 'tick', fn);
events.emit(ee, 'tick');
events.off(ee, 'tick', fn);
events.emit(ee, 'tick');
console.log(count);

// eventNames
events.on(ee, 'foo', function() {});
var names = events.eventNames(ee);
console.log(names.length > 0);

// removeAllListeners
events.removeAllListeners(ee, 'foo');
console.log(events.listenerCount(ee, 'foo'));

// listeners returns copy
events.on(ee, 'bar', function() {});
var ls = events.listeners(ee, 'bar');
console.log(ls.length);

// setMaxListeners / getMaxListeners
events.setMaxListeners(ee, 20);
console.log(events.getMaxListeners(ee));
