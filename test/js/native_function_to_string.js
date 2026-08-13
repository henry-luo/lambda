var object_source = Function.prototype.toString.call(Object);
var data_view_source = Function.prototype.toString.call(DataView);

console.log(object_source === "function Object() { [native code] }");
console.log(data_view_source === "function DataView() { [native code] }");
console.log(object_source !== data_view_source);
console.log(Function.prototype.toString.call(Object.bind(null)) ===
    "function () { [native code] }");
