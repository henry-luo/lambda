// Js58.2 density conversion: restoring the ES-scale sparse gap cap must not
// dense-fill the classic arr[999999] shape or sparse length-only arrays.

var arr = [0, 1, 2, 3, 4, 5];
arr[999999] = "edge";

var everyCount = 0;
var everyResult = arr.every(function(value, index) {
    everyCount++;
    return index < 6 || (index === 999999 && value === "edge");
});

var someCount = 0;
var someResult = arr.some(function(value, index) {
    someCount++;
    return index === 999999 && value === "edge";
});

console.log("length", arr.length);
console.log("every", everyResult, everyCount);
console.log("some", someResult, someCount);
console.log("hole-before-edge", 999998 in arr, arr[999998]);
console.log("edge", 999999 in arr, arr[999999]);
console.log("key-count", Object.keys(arr).length);

var lenOnly = [];
lenOnly.length = 999999;
console.log("len-only", lenOnly.length, 999998 in lenOnly, lenOnly[999998]);
lenOnly.push("tail");
console.log("push-after-sparse-length", lenOnly.length, 999998 in lenOnly, lenOnly[999999]);
