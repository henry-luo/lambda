function target(a, b, c) { return this.tag + ":" + a + b + c; }
var receiver = { tag: "R" };
var applyCount = 0;
var callableProxy = new Proxy(target, {
    apply: function (fn, thisArg, args) {
        applyCount++;
        return Reflect.apply(fn, thisArg, args);
    }
});
var first = callableProxy.bind(receiver, "a");
var second = first.bind({ tag: "ignored" }, "b");
console.log(second("c"));
console.log(applyCount + ":" + typeof callableProxy);
console.log("instanceof-function:" + (callableProxy instanceof Function));

var proxiedApplyTrap = new Proxy(function(fn, thisArg, args) {
    gc();
    return Reflect.apply(fn, thisArg, args);
}, {});
var proxyWithProxyTrap = new Proxy(function(value) { return value + 1; }, {
    apply: proxiedApplyTrap
});
console.log("proxy-trap-proxy:" + proxyWithProxyTrap(8));

var trapCount = 0;
var nonCallable = new Proxy({}, {
    apply: function () { trapCount++; return 1; }
});
try { nonCallable(); } catch (error) { console.log(error instanceof TypeError); }
console.log(trapCount + ":" + typeof nonCallable);

var fake = { call: function () { return 1; }, apply: function () { return 2; } };
try { fake(); } catch (error) { console.log(error instanceof TypeError); }
try { Function.prototype.apply.call(fake, null, []); }
catch (error) { console.log(error instanceof TypeError); }

function C(a, b) { this.value = a + b; }
var BoundC = C.bind({ ignored: true }, "x").bind(null, "y");
console.log(new BoundC().value);

var revocable = Proxy.revocable(target, {});
revocable.revoke();
console.log(typeof revocable.proxy);
try { revocable.proxy(); } catch (error) { console.log(error instanceof TypeError); }

var callbackCalls = 0;
var callback = new Proxy(function(value, index) {
    gc();
    callbackCalls++;
    return value + index;
}, { apply: function(fn, thisArg, args) { return Reflect.apply(fn, thisArg, args); } });
console.log([2, 3, 4].map(callback).join(",") + ":" + callbackCalls);
console.log([3, 1, 2].sort(new Proxy(function(a, b) { return a - b; }, {})).join(","));

function many() {
    gc();
    return arguments.length + ":" + arguments[0] + ":" + arguments[arguments.length - 1];
}
var manyBound = many.bind(null,
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9",
    "a10", "a11", "a12", "a13", "a14", "a15", "a16", "a17", "a18", "a19");
console.log(manyBound("tail"));

function ManyCtor() {
    gc();
    this.summary = arguments.length + ":" + arguments[0] + ":" + arguments[arguments.length - 1];
}
var ManyBoundCtor = ManyCtor.bind(null,
    "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8", "c9",
    "c10", "c11", "c12", "c13", "c14", "c15", "c16", "c17", "c18", "c19");
console.log(new ManyBoundCtor("end").summary);

var accessorState = { marker: 0 };
Object.defineProperty(accessorState, "value", {
    get: new Proxy(function() { gc(); return this.marker; }, {}),
    set: new Proxy(function(value) { gc(); this.marker = value; }, {}),
    configurable: true
});
accessorState.value = 17;
console.log("accessor-proxy:" + accessorState.value);

var jsonReviverCalls = 0;
var revived = JSON.parse('{"value":4}', new Proxy(function(key, value) {
    gc();
    jsonReviverCalls++;
    return value;
}, {}));
var jsonReplacerCalls = 0;
var replaced = JSON.stringify(revived, new Proxy(function(key, value) {
    gc();
    jsonReplacerCalls++;
    return value;
}, {}));
console.log(replaced + ":" + jsonReviverCalls + ":" + jsonReplacerCalls);

var executorCalls = 0;
new Promise(new Proxy(function(resolve) {
    gc();
    executorCalls++;
    resolve(1);
}, {}));
console.log("promise-executor-proxy:" + executorCalls);

var iteratorStep = 0;
var iterator = {
    next: new Proxy(function() {
        gc();
        return iteratorStep < 2 ? { value: ++iteratorStep, done: false } : { done: true };
    }, {})
};
var iterable = {};
iterable[Symbol.iterator] = new Proxy(function() { gc(); return iterator; }, {});
console.log("iterator-proxy:" + Array.from(iterable).join(","));
console.log("array-from-proxy:" + Array.from([1, 2], new Proxy(function(value) {
    gc();
    return value * 3;
}, {})).join(","));

var primitiveProxy = {};
primitiveProxy[Symbol.toPrimitive] = new Proxy(function(hint) {
    gc();
    return hint === "number" ? 23 : "primitive";
}, {});
console.log("to-primitive-proxy:" + (+primitiveProxy));

var SpeciesArray = class extends Uint8Array {};
Object.defineProperty(SpeciesArray, Symbol.species, {
    value: new Proxy(Uint8Array, {
        construct: function(target, args, newTarget) {
            gc();
            return Reflect.construct(target, args, newTarget);
        }
    })
});
var speciesValue = new SpeciesArray([3, 4]).map(function(value) { return value + 1; });
console.log("species-proxy:" + speciesValue[0] + "," + speciesValue[1]);

var collectionSource = new Map([["base", 1]]);
var collectionClone = new Map(collectionSource);
Reflect.ownKeys(collectionSource).forEach(function(key) {
    collectionClone[key] = collectionSource[key];
});
collectionClone.set("copy", 2);
console.log("collection-slots:" + Reflect.ownKeys(collectionSource).length + ":" +
    collectionSource.has("copy") + ":" + collectionSource.size + ":" + collectionClone.size);
