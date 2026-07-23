// Stage-1 boundary: JavaScript keeps reference-visible raw mutations.
var array = [1, 2];
var arrayAlias = array;
arrayAlias[0] = 9;
console.log("array:" + array[0] + "," + arrayAlias[0]);

var object = { value: 3, label: "plain" };
var objectAlias = object;
objectAlias.value = 8;
console.log("object:" + object.value + "," + objectAlias.value);

// This fixed-shape object exercises the direct shaped-slot setter path.
var shaped = { left: 4, right: 5 };
var shapedAlias = shaped;
shapedAlias.right = 12;
console.log("shape:" + shaped.left + "," + shaped.right);

globalThis.__cowRawAlias = { count: 1 };
var globalAlias = globalThis.__cowRawAlias;
globalAlias.count = 6;
console.log("global:" + globalThis.__cowRawAlias.count);

var map = new Map();
var mapAlias = map;
mapAlias.set("key", 11);
console.log("map:" + map.get("key"));
