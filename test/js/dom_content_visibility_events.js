var EventClass = ContentVisibilityAutoStateChangeEvent;
var event = new EventClass('sample', {skipped: true, bubbles: true});
console.log(event.type + ':' + event.skipped + ':' + event.bubbles + ':' + event.isTrusted);
console.log(event instanceof EventClass && event instanceof Event);
console.log(new EventClass('default').skipped);
console.log(new EventClass('null', null).skipped);
event.skipped = false;
console.log(event.skipped);
var descriptor = Object.getOwnPropertyDescriptor(EventClass.prototype, 'skipped');
console.log(typeof descriptor.get + ':' + typeof descriptor.set + ':' + descriptor.enumerable + ':' + descriptor.configurable);
try { descriptor.get.call({}); } catch (error) { console.log(error instanceof TypeError); }
try { EventClass('bad'); } catch (error) { console.log(error instanceof TypeError); }
try { new EventClass(); } catch (error) { console.log(error instanceof TypeError); }
try { new EventClass(Symbol()); } catch (error) { console.log(error instanceof TypeError); }
try { new EventClass('bad', 1); } catch (error) { console.log(error instanceof TypeError); }
var marker = {};
try { new EventClass('throw', {get skipped() {throw marker;}}); }
catch (error) {console.log(error === marker);}
var order = [];
var converted = new EventClass({toString: function() {order.push('type'); return 'converted';}}, {
    get bubbles() {order.push('bubbles'); return 0;},
    get cancelable() {order.push('cancelable'); return {};},
    get composed() {order.push('composed'); return '';},
    get skipped() {order.push('skipped'); return 'yes';}
});
console.log(order.join(','));
console.log(converted.type + ':' + converted.cancelable + ':' + converted.skipped);
class SubEvent extends EventClass {}
var derived = new SubEvent('derived', {skipped: true});
console.log(derived instanceof SubEvent && derived instanceof EventClass && derived instanceof Event);
console.log(derived.skipped);
