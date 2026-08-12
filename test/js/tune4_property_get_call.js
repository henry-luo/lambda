var savedMathAbs = Math.abs;
Math.abs = function (value) { return "math:" + value; };
console.log(Math.abs(4));
console.log(Math["abs"](5));
Math.abs = savedMathAbs;
console.log(Math.abs(-6));

var savedObjectKeys = Object.keys;
Object.keys = function () { return ["patched-keys"]; };
console.log(Object.keys({ a: 1 }).join(","));
Object.keys = savedObjectKeys;
console.log(Object.keys({ a: 1, b: 2 }).join(","));

var savedIsInteger = Number.isInteger;
Number.isInteger = function () { return "patched-integer"; };
console.log(Number.isInteger(1));
Number.isInteger = savedIsInteger;
console.log(Number.isInteger(1.5));

var savedFromCharCode = String.fromCharCode;
String.fromCharCode = function () { return "patched-char"; };
console.log(String.fromCharCode(65));
String.fromCharCode = savedFromCharCode;
console.log(String.fromCharCode(65));

var savedSymbolFor = Symbol.for;
Symbol.for = function () { return "patched-symbol"; };
console.log(Symbol.for("key"));
Symbol.for = savedSymbolFor;
console.log(typeof Symbol.for("key"));

var savedMap = Array.prototype.map;
Array.prototype.map = function () { return ["patched-map"]; };
console.log([1, 2].map(function (value) { return value * 2; }).join(","));
Array.prototype.map = savedMap;
console.log([1, 2].map(function (value) { return value * 2; }).join(","));

var savedPush = Array.prototype.push;
Array.prototype.push = function (value) { this[0] = value; return 41; };
var pushed = [];
console.log(pushed.push(9) + ":" + pushed[0]);
Array.prototype.push = savedPush;

var mapDescriptor = Object.getOwnPropertyDescriptor(Array.prototype, "map");
console.log(mapDescriptor.writable + ":" + mapDescriptor.enumerable + ":" + mapDescriptor.configurable);
delete Array.prototype.map;
console.log(typeof [1].map);
Object.defineProperty(Array.prototype, "map", mapDescriptor);
console.log([3].map(function (value) { return value + 1; })[0]);

var savedConsoleLog = console.log;
var consoleCalls = 0;
console.log = function () { consoleCalls++; };
console.log("hidden");
console.log = savedConsoleLog;
console.log("console:" + consoleCalls);

var accessorEvents = [];
var accessorReceiver = {
    get method() {
        accessorEvents[accessorEvents.length] = "get";
        return function (value) {
            accessorEvents[accessorEvents.length] = "call";
            return (this === accessorReceiver ? "this" : "wrong") + ":" + value;
        };
    }
};
function accessorArgument() {
    accessorEvents[accessorEvents.length] = "arg";
    return 7;
}
console.log(accessorReceiver.method(accessorArgument()));
console.log(accessorEvents.join(","));

var proxyEvents = [];
var proxyReceiver;
var proxyTarget = {
    method: function (value) {
        proxyEvents[proxyEvents.length] = "call";
        return (this === proxyReceiver ? "this" : "wrong") + ":" + value;
    }
};
proxyReceiver = new Proxy(proxyTarget, {
    get: function (target, key, receiver) {
        proxyEvents[proxyEvents.length] = "get:" + key;
        return Reflect.get(target, key, receiver);
    }
});
function proxyBase() {
    proxyEvents[proxyEvents.length] = "receiver";
    return proxyReceiver;
}
function proxyKey() {
    proxyEvents[proxyEvents.length] = "key";
    return "method";
}
function proxyArgument() {
    proxyEvents[proxyEvents.length] = "arg";
    return 8;
}
console.log(proxyBase()[proxyKey()](proxyArgument()));
console.log(proxyEvents.join(","));

var optionalKeyCalls = 0;
function optionalKey() { optionalKeyCalls++; return "method"; }
var optionalReceiver = null;
var optionalResult = optionalReceiver?.[optionalKey()]();
console.log(typeof optionalResult + ":" + optionalKeyCalls);

var typedArrayPrototype = Object.getPrototypeOf(Uint8Array.prototype);
var typedLengthDescriptor = Object.getOwnPropertyDescriptor(typedArrayPrototype, "length");
Object.defineProperty(typedArrayPrototype, "length", {
    get: function () { return 77; },
    configurable: true
});
console.log(new Uint8Array(3).length);
Object.defineProperty(typedArrayPrototype, "length", typedLengthDescriptor);
console.log(new Uint8Array(3).length);

var arrayBufferLengthDescriptor = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, "byteLength");
Object.defineProperty(ArrayBuffer.prototype, "byteLength", {
    get: function () { return 88; },
    configurable: true
});
console.log(new ArrayBuffer(4).byteLength);
Object.defineProperty(ArrayBuffer.prototype, "byteLength", arrayBufferLengthDescriptor);
console.log(new ArrayBuffer(4).byteLength);

var dataViewLengthDescriptor = Object.getOwnPropertyDescriptor(DataView.prototype, "byteLength");
Object.defineProperty(DataView.prototype, "byteLength", {
    get: function () { return 99; },
    configurable: true
});
console.log(new DataView(new ArrayBuffer(5)).byteLength);
Object.defineProperty(DataView.prototype, "byteLength", dataViewLengthDescriptor);
console.log(new DataView(new ArrayBuffer(5)).byteLength);

class StaticBox {
    static value = 3;
    static read() { return this.value; }
}
console.log(StaticBox.value + ":" + StaticBox.read());
Object.defineProperty(StaticBox, "value", {
    value: 12,
    writable: true,
    enumerable: true,
    configurable: true
});
console.log(StaticBox.value + ":" + StaticBox.read());
StaticBox.value += 2;
console.log(StaticBox.value);
StaticBox.read = function () { return "patched-read:" + this.value; };
console.log(StaticBox.read());

Object.defineProperty(savedMathAbs, "name", { value: "renamedAbs", configurable: true });
Object.defineProperty(savedMathAbs, "length", { value: 9, configurable: true });
console.log(savedMathAbs.name + ":" + savedMathAbs.length + ":" + savedMathAbs(-10));

var borrowed = { 0: "first", 1: "second", length: 2 };
var callbackObjectIsBorrowed = false;
var borrowedFound = Array.prototype.find.call(borrowed, function(value, index, object) {
    [3, 2, 1].sort(function(a, b) { return a - b; });
    callbackObjectIsBorrowed = callbackObjectIsBorrowed || object === borrowed;
    return index === 1;
});
console.log(borrowedFound + ":" + callbackObjectIsBorrowed);

var joinReceiver = {
    join: new Proxy(function() { return "proxy-join:" + (this === joinReceiver); }, {})
};
console.log(Array.prototype.toString.call(joinReceiver));

var flatMapTyped = new Int32Array([1, 0, 42]);
Object.defineProperty(flatMapTyped, "constructor", {
    get: function () { throw new Error("ArraySpeciesCreate read a non-Array constructor"); }
});
var flatMapResult = Array.prototype.flatMap.call(flatMapTyped, function(value) {
    return value;
});
console.log(flatMapResult.join(",") + ":" + (flatMapResult instanceof Array));
