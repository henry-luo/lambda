// freezing protects own properties while collection entries remain mutable.

// --- test 1: frozen Map allows set() ---
var m = new Map();
m.set("a", 1);
Object.freeze(m);
try {
    m.set("b", 2);
    console.log("t1:no error");
} catch (e) {
    console.log("t1:" + e.message);
}
// existing entries remain readable and the new entry is stored
console.log("t1b:" + m.get("a"));
console.log("t1c:" + m.get("b") + "," + m.size);

// --- test 2: frozen Map allows delete() ---
var m2 = new Map();
m2.set("x", 10);
Object.freeze(m2);
try {
    m2.delete("x");
    console.log("t2:no error");
} catch (e) {
    console.log("t2:" + e.message);
}

console.log("t2b:" + m2.has("x") + "," + m2.size);

// --- test 3: frozen Map allows clear() ---
var m3 = new Map();
m3.set("a", 1);
Object.freeze(m3);
try {
    m3.clear();
    console.log("t3:no error");
} catch (e) {
    console.log("t3:" + e.message);
}

console.log("t3b:" + m3.has("a") + "," + m3.size);

// --- Test 4: Frozen Map allows read operations ---
var m4 = new Map();
m4.set("key", "value");
Object.freeze(m4);
console.log("t4:" + m4.get("key") + "," + m4.has("key") + "," + m4.size);

// --- test 5: frozen Set allows add() ---
var s = new Set();
s.add(1);
Object.freeze(s);
try {
    s.add(2);
    console.log("t5:no error");
} catch (e) {
    console.log("t5:" + e.message);
}
console.log("t5b:" + s.has(1) + "," + s.size);
console.log("t5c:" + s.has(2));

// --- test 6: frozen Set allows delete() ---
var s2 = new Set([1, 2, 3]);
Object.freeze(s2);
try {
    s2.delete(1);
    console.log("t6:no error");
} catch (e) {
    console.log("t6:" + e.message);
}

console.log("t6b:" + s2.has(1) + "," + s2.has(2) + "," + s2.size);

// --- test 7: frozen Set allows clear() ---
var s3 = new Set([1]);
Object.freeze(s3);
try {
    s3.clear();
    console.log("t7:no error");
} catch (e) {
    console.log("t7:" + e.message);
}

console.log("t7b:" + s3.has(1) + "," + s3.size);

// --- Test 8: Non-frozen collections work normally ---
var m5 = new Map();
m5.set("a", 1);
m5.set("b", 2);
m5.delete("a");
console.log("t8:" + m5.size + "," + m5.has("b"));

// own property protection is independent of collection internal data.
function checkFrozenProperties(collection, label) {
    collection.tag = "before";
    var identity = Object.freeze(collection) === collection;
    var descriptor = Object.getOwnPropertyDescriptor(collection, "tag");
    console.log(label + ":own:" + identity + "," + Object.isFrozen(collection) + "," +
        Object.isExtensible(collection) + "," + descriptor.writable + "," + descriptor.configurable);
    console.log(label + ":reflect:" + Reflect.set(collection, "tag", "after") + "," +
        Reflect.set(collection, "extra", 1) + "," + Reflect.deleteProperty(collection, "tag"));
    console.log(label + ":kept:" + collection.tag + "," + ("extra" in collection));
    var strictRejected = false;
    try {
        (function() { "use strict"; collection.tag = "after"; })();
    } catch (e) {
        strictRejected = e instanceof TypeError;
    }
    console.log(label + ":strict:" + strictRejected);
    return collection;
}
var frozenMap = checkFrozenProperties(new Map(), "t9map");
frozenMap.set("tag", 7);
console.log("t9map:data:" + frozenMap.get("tag") + "," + frozenMap.tag + "," + Object.isFrozen(frozenMap));
var frozenSet = checkFrozenProperties(new Set(), "t9set");
frozenSet.add("tag");
console.log("t9set:data:" + frozenSet.has("tag") + "," + frozenSet.tag + "," + Object.isFrozen(frozenSet));
