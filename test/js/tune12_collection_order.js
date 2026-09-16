// T12-1: the hash index points at stable order nodes. These probes exercise
// SameValueZero, update/delete order, iterator continuity and hash growth.
var equality = new Map();
var first_same = ["same"].join("");
var second_same = ["sa", "me"].join("");
var object_key = {};
equality.set(first_same, "string");
equality.set(NaN, "nan");
equality.set(-0, "zero");
equality.set(object_key, "object");
console.log("equal:" + equality.get(second_same) + "," +
    equality.get(NaN) + "," + equality.get(0) + "," +
    equality.get(object_key) + "," + equality.size);

var order = new Map();
order.set("a", 1);
order.set("b", 2);
order.set("a", 3);
console.log("update:" + Array.from(order.keys()).join(",") + "," +
    order.get("a"));
order.delete("a");
order.set("a", 4);
console.log("reinsert:" + Array.from(order.keys()).join(",") + "," +
    order.get("a"));

var active = new Map();
active.set("a", 1);
active.set("b", 2);
active.set("c", 3);
var active_iter = active.keys();
var active_first = active_iter.next().value;
active.delete("b");
active.set("d", 4);
var active_second = active_iter.next().value;
var active_third = active_iter.next().value;
console.log("iterator:" + active_first + "," + active_second + "," +
    active_third + "," + active_iter.next().done);

var cleared = new Map();
cleared.set("a", 1);
var cleared_iter = cleared.keys();
cleared.clear();
cleared.set("b", 2);
console.log("clear:" + cleared_iter.next().value + "," +
    cleared_iter.next().done);

var exhausted = new Map();
var exhausted_iter = exhausted.keys();
var exhausted_first = exhausted_iter.next().done;
exhausted.set("later", 1);
console.log("exhausted:" + exhausted_first + "," + exhausted_iter.next().done);

var callback_map = new Map();
callback_map.set("a", 1);
callback_map.set("b", 2);
callback_map.set("c", 3);
var callback_seen = [];
callback_map.forEach(function(value, key) {
    callback_seen.push(key);
    if (key === "a") {
        callback_map.delete("b");
        callback_map.set("d", 4);
    }
    if (key === "c") {
        callback_map.clear();
        callback_map.set("e", 5);
    }
});
console.log("foreach:" + callback_seen.join(",") + "," +
    Array.from(callback_map.keys()).join(","));

var grown = new Map();
for (var i = 0; i < 256; i++) {
    grown.set("k" + i, i);
}
grown.set("k32", 333);
grown.delete("k10");
grown.set("k10", 10);
var grown_keys = Array.from(grown.keys());
console.log("growth:" + grown.size + "," + grown.get("k32") + "," +
    grown_keys[0] + "," + grown_keys[255]);

var first_set = new Set();
var second_set = new Set();
first_set.add("a");
second_set.add("a");
first_set.add("b");
console.log("independent:" + first_set.size + "," + second_set.size + "," +
    Array.from(first_set).join(",") + "," + Array.from(second_set).join(","));

var scalar_homes = new Map();
var scalar_key = 1.25;
var scalar_value = 9007199254740991;
scalar_homes.set(scalar_key, scalar_value);
for (var allocation = 0; allocation < 2048; allocation++) {
    ("temporary-" + allocation).toUpperCase();
}
console.log("scalar-homes:" + scalar_homes.get(scalar_key) + "," +
    scalar_homes.has(1.25));
