// D3.4.5–D3.4.6: reserved constructor slots remain absent until assignment.
function Pair(first, second) { this.first = first; this.second = second; }
function firstOf(pair) { return pair.first; }
const absent = new Pair(null, null);
const numeric = new Pair(3.5, 5e-324);
const text = new Pair('text', false);
console.log(firstOf(absent), firstOf(numeric), firstOf(text));
console.log(numeric.second === 5e-324, text.second, absent instanceof Pair);
console.log(Object.keys(numeric).join(','), Object.getPrototypeOf(numeric) === Pair.prototype);
numeric.first = 'changed';
console.log(firstOf(numeric), firstOf(absent), firstOf(new Pair(9, 10)));

let escaped;
function inspect(object) {
    escaped = object;
    console.log('before', 'first' in object, 'second' in object, Object.keys(object).join(','));
    return 1;
}
function Escaping() { this.first = inspect(this); this.second = this.first + 2; }
const escapedResult = new Escaping();
console.log(escaped === escapedResult, escapedResult.first, escapedResult.second);
let thrown;
function fail(object) { thrown = object; throw new Error('stop'); }
function Abrupt() { this.first = fail(this); this.second = 2; }
try { new Abrupt(); } catch (e) { console.log(e.message); }
console.log('first' in thrown, 'second' in thrown, Object.keys(thrown).length);

let setterCalls = 0;
function Setter() { this.first = 7; this.second = 8; }
Object.defineProperty(Setter.prototype, 'first', {set(v) { setterCalls += v; }, configurable: true});
const inherited = new Setter();
console.log(setterCalls, inherited.first, inherited.second, Object.keys(inherited).join(','));
function Freeze() { this.first = Object.preventExtensions(this); this.second = 2; }
const frozen = new Freeze();
console.log('first' in frozen, 'second' in frozen, Object.isExtensible(frozen));

function Replacement() { this.first = 1; this.second = 2; return {first: 11}; }
console.log(firstOf(new Replacement()));
const Bound = Pair.bind(null, 21);
const bound = new Bound(22);
console.log(bound.first, bound.second, bound instanceof Pair);
function Other() {}
const reflected = Reflect.construct(Pair, [31, 32], Other);
console.log(reflected.first, reflected.second, Object.getPrototypeOf(reflected) === Other.prototype);

// Different definitions can share a slot ordinal; the live NameId must match.
function left(v) { return {leftOnly: v, shared: true}; }
function right(v) { return {rightOnly: v, shared: false}; }
function readLeft(v) { return v.leftOnly; }
const a = left(1), b = right(2);
console.log(readLeft(a), readLeft(b));
a.leftOnly = 'lane change';
console.log(readLeft(a), left(3).leftOnly);
delete a.leftOnly;
console.log(readLeft(a));
Object.defineProperty(a, 'leftOnly', {get() { return 'accessor'; }, configurable: true});
console.log(readLeft(a));
const fixed = Object.freeze(left(8));
(function() {'use strict'; try { fixed.leftOnly = 9; } catch(e) { console.log(e instanceof TypeError); }})();

// Shared physical slots retain bool, string, Symbol and wide numeric ownership.
function scalar(v) { return {scalarSlot: v}; }
function readScalar(v) { return v.scalarSlot; }
const sym = Symbol('slot');
for (const v of [false, true, 'string', sym, 5e-324, -0, null, 99]) {
    const holder = scalar(v);
    console.log(Object.is(readScalar(holder), v));
    holder.scalarSlot = v;
    console.log(Object.is(readScalar(holder), v));
}
const owners = [];
for (let i = 0; i < 250; i++) owners.push(new Pair('v' + i, i % 2 ? 5e-324 : null));
console.log(owners[0].first, owners[249].first, owners[249].second === 5e-324);

// Storage reservation must not preassign property insertion order.
function Reordered() { this.first = (this.other = 1, 2); this.second = 3; }
console.log(Object.keys(new Reordered()).join(','));
function Defined() {
    this.first = (Object.defineProperty(this, 'second', {
        value: 9, enumerable: true, configurable: true, writable: true
    }), 2);
    this.second = 3;
}
console.log(Object.keys(new Defined()).join(','));
function Defaulted(value = (this.other = 1)) { this.first = value; this.second = 3; }
console.log(Object.keys(new Defaulted()).join(','));
function SetOrder() { this.first = 1; this.second = 2; }
Object.defineProperty(SetOrder.prototype, 'first', {set(v) { this.other = v; }});
console.log(Object.keys(new SetOrder()).join(','));
