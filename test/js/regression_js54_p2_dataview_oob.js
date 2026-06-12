// Js54 P2 regression: DataView OOB-aware accessors over resizable buffers.
// - fixed-length DV byteLength stays constant across grow, throws on shrink-past-end
// - length-tracking DV byteLength updates live with the buffer
// - OOB DV throws TypeError on byteLength/byteOffset/get*/set*
// - detached buffer throws TypeError on byteLength/byteOffset/get*/set*

function assertEq(actual, expected, label) {
    if (actual !== expected) throw new Error(label + ": got " + actual + " expected " + expected);
}
function assertThrows(fn, kind, label) {
    let threw = false;
    try { fn(); } catch (e) {
        threw = true;
        if (e.constructor.name !== kind) throw new Error(label + ": threw " + e.constructor.name + " expected " + kind);
    }
    if (!threw) throw new Error(label + ": did not throw " + kind);
}

// fixed-length DV stays valid after grow
const rab1 = new ArrayBuffer(16, {maxByteLength: 40});
const dv1 = new DataView(rab1, 0, 4);
assertEq(dv1.byteLength, 4, "dv1 initial byteLength");
assertEq(dv1.byteOffset, 0, "dv1 initial byteOffset");
dv1.setInt8(0, 99);
assertEq(dv1.getInt8(0), 99, "dv1 get after set");
rab1.resize(32);
assertEq(dv1.byteLength, 4, "dv1 byteLength after grow");
assertEq(dv1.getInt8(0), 99, "dv1 data survived grow");

// fixed-length DV throws on shrink-past-end
const rab2 = new ArrayBuffer(16, {maxByteLength: 40});
const dv2 = new DataView(rab2, 4, 8);  // view: [4..12]
assertEq(dv2.byteLength, 8, "dv2 initial byteLength");
rab2.resize(10);  // buffer end=10, view end=12 — OOB
assertThrows(() => dv2.byteLength, "TypeError", "dv2 byteLength after OOB shrink");
assertThrows(() => dv2.byteOffset, "TypeError", "dv2 byteOffset after OOB shrink");
assertThrows(() => dv2.getInt8(0), "TypeError", "dv2 getInt8 after OOB shrink");
assertThrows(() => dv2.setInt8(0, 1), "TypeError", "dv2 setInt8 after OOB shrink");

// length-tracking DV byteLength updates on resize
const rab3 = new ArrayBuffer(16, {maxByteLength: 40});
const dv3 = new DataView(rab3, 4);  // length-tracking
assertEq(dv3.byteLength, 12, "dv3 initial byteLength");
rab3.resize(20);
assertEq(dv3.byteLength, 16, "dv3 byteLength after grow");
rab3.resize(8);
assertEq(dv3.byteLength, 4, "dv3 byteLength after shrink");
rab3.resize(4);  // offset 4 == buffer end → byteLength 0 (still in bounds)
assertEq(dv3.byteLength, 0, "dv3 byteLength at boundary");

// length-tracking DV throws when offset > buffer length
const rab4 = new ArrayBuffer(16, {maxByteLength: 40});
const dv4 = new DataView(rab4, 8);
assertEq(dv4.byteLength, 8, "dv4 initial byteLength");
rab4.resize(4);
assertThrows(() => dv4.byteLength, "TypeError", "dv4 byteLength when offset > buffer");
assertThrows(() => dv4.getInt8(0), "TypeError", "dv4 getInt8 when OOB");

// detached buffer throws on getters
const ab5 = new ArrayBuffer(8);
const dv5 = new DataView(ab5);
assertEq(dv5.byteLength, 8, "dv5 initial byteLength");
$262.detachArrayBuffer(ab5);
assertThrows(() => dv5.byteLength, "TypeError", "dv5 byteLength after detach");
assertThrows(() => dv5.byteOffset, "TypeError", "dv5 byteOffset after detach");
assertThrows(() => dv5.getInt8(0), "TypeError", "dv5 getInt8 after detach");
assertThrows(() => dv5.setInt8(0, 1), "TypeError", "dv5 setInt8 after detach");

console.log("Js54 P2 DataView OOB regression: all assertions passed");
