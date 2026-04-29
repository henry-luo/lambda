// Invariant: get on FLOAT key matches a property defined under the
// canonical string form of that float ("1.5"). ToPropertyKey must be
// applied at the entry of js_property_get.
var obj = {};
Object.defineProperty(obj, "1.5", { value: "v15", configurable: true });
console.log(obj[1.5]);
console.log("OK");
