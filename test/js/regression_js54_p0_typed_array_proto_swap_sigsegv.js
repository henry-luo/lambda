// Js54 P0 regression: SIGSEGV on indexed write after a typed-array property
// write triggers the map-storage upgrade.
//
// Original test262 surface: built-ins/TypedArray/out-of-bounds-behaves-like-detached.js
// Reduced form: any user property write to a typed array (e.g. ta.__proto__=X,
// ta.foo='bar') migrates the JsTypedArray* into the __ta__ slot and repoints
// Map.data at the new property-storage buffer. The MIR JIT inline indexed-set
// path used to load Map.data at offset 16 and treat it as a JsTypedArray*,
// then dereference *(void**)(ta_ptr + 16) as the element buffer — wild memory,
// SIGSEGV on the next sized store.
//
// Fix: route MIR inline ta-set/get/get_native and the loop hoist through
// js_get_typed_array_ptr, which honors data_cap and reads the __ta__ slot
// when upgraded.

const ta = new Int8Array(4);
ta[0] = 5;
if (ta[0] !== 5) throw new Error("pre-upgrade write failed");

// trigger map storage upgrade
ta.foo = "bar";
if (ta.foo !== "bar") throw new Error("property write failed");
if (ta.length !== 4) throw new Error("length lost after upgrade");
if (ta[0] !== 5) throw new Error("element lost after upgrade");

// post-upgrade indexed write used to SIGSEGV
ta[1] = 20;
if (ta[1] !== 20) throw new Error("post-upgrade write failed");

// __proto__-swap variant (the original test262 trigger)
const tb = new Int8Array(4);
tb.__proto__ = { 2: "wrong" };
tb[2] = 10;
if (tb[2] !== 10) throw new Error("post-proto-swap write failed");

// resizable-buffer integration: post-resize OOB access returns undefined and
// `in` reports false (the full test262 contract).
const rab = new ArrayBuffer(16, { maxByteLength: 40 });
const tc = new Int8Array(rab, 0, 4);
tc.__proto__ = { 2: "wrong" };
tc[2] = 11;
if (tc[2] !== 11) throw new Error("resizable + proto-swap write failed");
rab.resize(0);
if (tc[2] !== undefined) throw new Error("post-resize OOB should be undefined");
if (2 in tc) throw new Error("post-resize OOB should not be `in` tc");
