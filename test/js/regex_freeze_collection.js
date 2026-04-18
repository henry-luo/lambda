// Test Object.freeze enforcement on Map and Set collections

// --- Test 1: Frozen Map rejects set() ---
var m = new Map();
m.set("a", 1);
Object.freeze(m);
try {
    m.set("b", 2);
    console.log("t1:no error");
} catch (e) {
    console.log("t1:" + e.message);
}
// Existing entry should still be readable
console.log("t1b:" + m.get("a"));

// --- Test 2: Frozen Map rejects delete() ---
var m2 = new Map();
m2.set("x", 10);
Object.freeze(m2);
try {
    m2.delete("x");
    console.log("t2:no error");
} catch (e) {
    console.log("t2:" + e.message);
}

// --- Test 3: Frozen Map rejects clear() ---
var m3 = new Map();
m3.set("a", 1);
Object.freeze(m3);
try {
    m3.clear();
    console.log("t3:no error");
} catch (e) {
    console.log("t3:" + e.message);
}

// --- Test 4: Frozen Map allows read operations ---
var m4 = new Map();
m4.set("key", "value");
Object.freeze(m4);
console.log("t4:" + m4.get("key") + "," + m4.has("key") + "," + m4.size);

// --- Test 5: Frozen Set rejects add() ---
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

// --- Test 6: Frozen Set rejects delete() ---
var s2 = new Set([1, 2, 3]);
Object.freeze(s2);
try {
    s2.delete(1);
    console.log("t6:no error");
} catch (e) {
    console.log("t6:" + e.message);
}

// --- Test 7: Frozen Set rejects clear() ---
var s3 = new Set([1]);
Object.freeze(s3);
try {
    s3.clear();
    console.log("t7:no error");
} catch (e) {
    console.log("t7:" + e.message);
}

// --- Test 8: Non-frozen collections work normally ---
var m5 = new Map();
m5.set("a", 1);
m5.set("b", 2);
m5.delete("a");
console.log("t8:" + m5.size + "," + m5.has("b"));
