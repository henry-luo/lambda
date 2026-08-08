// Tune1: Error and primitive throws share the in-band Item lane.
let error = new Error("tune1");
console.log(error.hasOwnProperty("message"));
console.log(error.hasOwnProperty("stack"));
console.log(Object.keys(error).length);
let descriptor = Object.getOwnPropertyDescriptor(error, "message");
console.log(descriptor.enumerable);
console.log(descriptor.writable);
console.log(descriptor.configurable);
console.log(typeof error.stack);
console.log(error.stack === error.stack);

try {
  throw 7;
} catch (value) {
  console.log(typeof value);
  console.log(value === 7);
}

try {
  throw error;
} catch (caught) {
  console.log(caught === error);
  console.log(caught instanceof Error);
  console.log(caught.message);
}

let nested = () => { throw error; };
try {
  nested();
} catch (caught) {
  console.log(caught === error);
  console.log(caught instanceof Error);
}

let accessor = {};
Object.defineProperty(accessor, "value", {
  get: () => { throw new Error("accessor"); }
});
try {
  accessor.value;
} catch (caught) {
  console.log(caught.message);
  console.log(caught instanceof Error);
}

let finally_result = "";
try {
  throw "finally";
} catch (value) {
  finally_result = value;
} finally {
  finally_result += ":done";
}
console.log(finally_result);
