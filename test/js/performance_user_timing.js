// User Timing records real timestamps, retained entries and cloned metadata.
function check(value, label) { if (!value) throw new Error(label); console.log(true); }
function rejects(body, name) { try { body(); } catch (e) { return e.name === name; } return false; }
const Mark = PerformanceMark;
const Measure = PerformanceMeasure;
const Entry = PerformanceEntry;
check(Object.getPrototypeOf(Mark) === Entry && Object.getPrototypeOf(Mark.prototype) === Entry.prototype, 'interface inheritance');
performance.clearMarks(); performance.clearMeasures();
const original = { nested: { value: 4 } };
const standalone = new Mark('standalone', {startTime: 11, detail: original});
original.nested.value = 9;
check(standalone instanceof Mark && standalone instanceof Entry && standalone.startTime === 11 && standalone.duration === 0, 'constructed mark');
check(standalone.detail !== original && standalone.detail.nested.value === 4 && standalone.detail === standalone.detail, 'cloned detail');
check(performance.getEntriesByName('standalone').length === 0, 'constructor does not buffer');
check(Object.prototype.toString.call(standalone) === '[object PerformanceMark]', 'mark tag');
const start = performance.now(); const live = performance.mark('live'); const finish = performance.now();
check(live.startTime >= start && live.startTime <= finish && live.detail === null, 'document clock');
check(performance.getEntriesByName('live', 'mark')[0] === live && performance.getEntries() !== performance.getEntries(), 'query identity and snapshots');
let nameReads = 0, typeReads = 0;
const queryName = {toString() { nameReads++; return 'live'; }};
const queryType = {toString() { typeReads++; return 'mark'; }};
check(performance.getEntriesByName(queryName, queryType)[0] === live && nameReads === 1 && typeReads === 1, 'query conversion once');
const getter = Object.getOwnPropertyDescriptor(Entry.prototype, 'startTime').get;
check(getter.call(standalone) === 11 && rejects(() => getter.call({}), 'TypeError') && rejects(() => getter.call(new Proxy(standalone, {})), 'TypeError'), 'captured getter brand');
check(rejects(() => Mark('bad'), 'TypeError') && rejects(() => new Measure('bad'), 'TypeError'), 'constructor capabilities');
check(rejects(() => performance.mark.call({}, 'bad'), 'TypeError') && rejects(() => Performance.prototype.getEntries.call({}), 'TypeError'), 'method receivers');
check(rejects(() => performance.mark('bad', {startTime: -1}), 'TypeError') && rejects(() => performance.mark('bad', {startTime: Infinity}), 'TypeError'), 'invalid timestamps');
check(rejects(() => performance.mark('bad', {detail: () => 1}), 'DataCloneError'), 'uncloneable detail');
const token = {}; let identical = false;
try { performance.mark('bad', {get startTime() { throw token; }}); } catch (e) { identical = e === token; }
check(identical, 'conversion exception identity');
performance.mark('duplicate', {startTime: 30}); const recent = performance.mark('duplicate', {startTime: 5});
const interval = performance.measure('interval', 'duplicate');
check(interval instanceof Measure && interval instanceof Entry && interval.startTime === 30 && interval.entryType === 'measure', 'latest timestamp mark');
const explicit = performance.measure('explicit', {start: 2, duration: 3, detail: {value: 7}});
check(explicit.startTime === 2 && explicit.duration === 3 && explicit.detail.value === 7, 'measure options');
const back = performance.measure('back', {end: 2, duration: 4});
check(back.startTime === -2 && back.duration === 4 && performance.measure('empty', {}).startTime === 0, 'derived start and empty options');
check(rejects(() => performance.measure('bad', 'missing'), 'SyntaxError') && rejects(() => performance.measure('bad', {duration: 2}), 'TypeError') && rejects(() => performance.measure('bad', {start: 0, end: 4, duration: 4}), 'TypeError'), 'measure validation');
const json = standalone.toJSON();
check(json.name === 'standalone' && json.entryType === 'mark' && json.startTime === 11 && json.duration === 0 && !('detail' in json) && json !== standalone.toJSON(), 'entry serialization');
const shadow = performance.mark('shadow-origin', {startTime: 6});
Object.defineProperty(shadow, 'name', {value: 'shadow'}); Object.defineProperty(shadow, 'startTime', {value: 999});
check(performance.getEntriesByName('shadow-origin').includes(shadow) && performance.measure('shadow-safe', 'shadow-origin').startTime === 6, 'internal metadata');
const marks = performance.getEntriesByType('mark');
check(marks.indexOf(recent) < marks.findIndex(e => e !== recent && e.name === 'duplicate'), 'chronological query');
performance.clearMarks('duplicate');
check(performance.getEntriesByName('duplicate').length === 0 && performance.getEntriesByType('measure').includes(explicit), 'selective clearing');
const savedConstructor = globalThis.PerformanceMark; globalThis.PerformanceMark = function Replacement() {};
const retained = performance.mark('retained'); globalThis.PerformanceMark = savedConstructor;
check(retained instanceof Mark, 'canonical constructor');
class ChildMark extends Mark {}
check(new ChildMark('child') instanceof ChildMark, 'subclass construction');
function Alternate() {} Alternate.prototype = null;
check(Object.getPrototypeOf(Reflect.construct(Mark, ['alternate'], Alternate)) === Object.prototype, 'intrinsic prototype fallback');
performance.clearMarks(); performance.clearMeasures();
check(performance.getEntriesByType('mark').length === 0 && performance.getEntriesByType('measure').length === 0 && standalone.detail.nested.value === 4, 'clearing retains held entries');
const buffered = performance.mark('buffered', {startTime: 2}); let notifications = 0; let delivered = [];
const observer = new PerformanceObserver((list, owner) => {
    notifications++; delivered = list.getEntries();
    check(owner === observer && list instanceof PerformanceObserverEntryList && list.getEntriesByType('mark').length === 2, 'observer list');
});
observer.observe({type: 'mark', buffered: true}); observer.observe({type: 'measure'});
const later = performance.mark('later', {startTime: 3}); const span = performance.measure('span', {start: 4, end: 8});
check(notifications === 0 && PerformanceObserver.supportedEntryTypes.includes('mark') && PerformanceObserver.supportedEntryTypes.includes('measure'), 'asynchronous delivery and supported types');
setTimeout(() => {
    check(notifications === 1 && delivered.length === 3 && delivered[0] === buffered && delivered[1] === later && delivered[2] === span, 'observer identity and order');
    observer.disconnect(); performance.mark('after-disconnect');
    setTimeout(() => { check(notifications === 1, 'disconnect'); console.log('USER_TIMING_DONE'); }, 0);
}, 0);
