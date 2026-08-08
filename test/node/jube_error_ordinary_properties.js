// Unified Error carriers must retain arbitrary own properties outside the
// propagation lane, including descriptor and deletion behavior.
const error = new Error("carrier");
error.code = "ERR_CARRIER";
error.detail = 42;

console.log(error.code, error.detail);
console.log(error.hasOwnProperty("code"));
const descriptor = Object.getOwnPropertyDescriptor(error, "code");
console.log(descriptor.value, descriptor.enumerable, descriptor.writable,
  descriptor.configurable);
const keys = Object.keys(error);
console.log(keys.length, keys[0], keys[1]);
console.log(Object.getOwnPropertyNames(error).indexOf("code") >= 0);
delete error.code;
console.log(error.code, error.hasOwnProperty("code"));
