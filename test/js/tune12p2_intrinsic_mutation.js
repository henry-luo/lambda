// T12-P2-2: ordinary writes avoid the unrelated intrinsic scan, while writes
// to Object.prototype and Array.prototype still invalidate inherited indices.
var hole = new Array(1);
Object.prototype[0] = "object-proto";
var object_value = hole[0];
delete Object.prototype[0];

Array.prototype[0] = "array-proto";
var array_value = hole[0];
delete Array.prototype[0];

var ordinary = {};
ordinary.field = 7;
console.log("object:" + object_value);
console.log("array:" + array_value);
console.log("ordinary:" + ordinary.field);
